/* 设备端解码日志实现（内部 Flash 尾部环形缓冲）。记录格式与容量说明见 pktlog.h。
 * 设计要点：
 *   1) 一槽 72 字节、页 1024 字节；页是 F103 的最小擦除单位，槽不跨页。
 *   2) seq 最后写：半条记录（seq 还是 0xFFFF）在开机扫描时被忽略，掉电不会读出坏数据。
 *   3) 写 Flash 会阻塞 CPU（页擦除约 20~40ms）：调用方 bbcall_app.c 故意延后 200ms
 *      再写，等这一包彻底收完，避免停顿吃掉下一包。 */
#include "pktlog.h"
#ifndef PKTLOG_HOST
#include "bbcall_cfg.h"
#else
#ifndef BBCALL_PKTLOG_PAGES
#define BBCALL_PKTLOG_PAGES 16u     /* 主机回归：页数上限，与 bbcall_cfg.h 一致 */
#endif
#endif
#include "bbcall_hw.h"
#include "rtc_math.h"
#ifndef PKTLOG_HOST
#include "stm32f1xx_hal.h"
#else
/* 主机回归（tools/test_pktlog.c）：Flash 换成内存数组，HAL 用替身函数，
 * 这样环形写入/掉电半条/开机扫描这些逻辑不用烧板子就能验证。 */
uint16_t pktlog_host_rd16(uint32_t addr);
uint8_t  pktlog_host_program(uint32_t addr, uint16_t val);
void     pktlog_host_erase(uint32_t addr);
#ifndef PKTLOG_HOST_BASE
#define PKTLOG_HOST_BASE 0x0800E000u    /* 主机回归：内存数组里日志区的起点 */
#endif
#ifndef PKTLOG_HOST_PAGES
#define PKTLOG_HOST_PAGES 8u            /* 主机回归：8 页 = 112 条 */
#endif
#endif

#define PKTLOG_PAGE_BYTES     1024u
#define PKTLOG_SLOT_BYTES     72u
#define PKTLOG_WORDS          (PKTLOG_SLOT_BYTES / 2u)                    /* 36 个半字 */
#define PKTLOG_SLOTS_PER_PAGE (PKTLOG_PAGE_BYTES / PKTLOG_SLOT_BYTES)     /* 14 槽/页 */

#define PKTLOG_FLASH_TOP 0x08010000u                                      /* STM32F103C8 = 64KB Flash */

#define PKTLOG_MAGIC 0x5Au
#define PKTLOG_EMPTY 0xFFFFu

/* 日志区放在**固件映像之后**（链接脚本导出 _image_end），不再从尾部硬扣固定 8KB：
 * 容量 = 剩余页数（上限 BBCALL_PKTLOG_PAGES），于是固件变大时日志自动缩小，
 * Release(-Os) 比 Debug(-Og) 能多存上百条；也免掉了"改 PAGES 必须同步改 .ld"的坑。
 * 约束：日志区必须落在固件映像之后，所以 _image_end 的取值由链接脚本保证（见 .ld 注释）。 */

static uint8_t  s_ready;      /* pktlog_init 完成后才允许写 */
static uint32_t s_base;       /* 日志区起始地址（init 时按 _image_end 之后第一页算） */
static uint16_t s_pages;      /* 日志区页数 */
static uint16_t s_cap;        /* 容量 = s_pages x 14 条 */
static uint16_t s_count;      /* 当前有效记录数 */
static uint16_t s_seq_next;   /* 下一条的序号 = 累计写入条数（16bit 回绕） */
static uint16_t s_page;       /* 当前写入页 */
static uint16_t s_slot;       /* 当前页下一个空槽；== SLOTS_PER_PAGE 表示该页已满 */

static uint16_t rd16(uint32_t addr)
{
#ifdef PKTLOG_HOST
  return pktlog_host_rd16(addr);
#else
  return *(volatile uint16_t *)addr;
#endif
}

static uint32_t slot_addr_of(uint16_t page, uint16_t slot)
{
  return s_base + ((uint32_t)page * PKTLOG_PAGE_BYTES) + ((uint32_t)slot * PKTLOG_SLOT_BYTES);
}

/* 线性下标 -> Flash 地址：槽不跨页，所以必须按"页 x 1024 + 槽 x 72"换算（不能拿 72 直接乘下标） */
static uint32_t idx_addr(uint16_t idx)
{
  return slot_addr_of((uint16_t)(idx / PKTLOG_SLOTS_PER_PAGE),
                      (uint16_t)(idx % PKTLOG_SLOTS_PER_PAGE));
}

/* 开一条记录的"像不像有效数据"检查：seq 非空 + magic + meta 保留位 + info_len + 呼号可打印。
 * 目的：旧固件镜像或残字节落在这 8KB 里时，不被当成日志记录。 */
static uint8_t slot_probe(uint32_t a, uint16_t *seq_out)
{
  uint16_t seq = rd16(a);
  if (seq == PKTLOG_EMPTY) return 0u;
  if ((rd16(a + 6u) & 0x00E0u) != 0u) return 0u;                       /* meta bit5~7 必须为 0 */
  if ((rd16(a + 8u) >> 8) > PKTLOG_INFO_MAX) return 0u;
  if ((rd16(a + 18u) & 0xFF00u) != ((uint16_t)PKTLOG_MAGIC << 8)) return 0u;
  for (uint8_t i = 0u; i < 6u; i++) {
    uint8_t c = (uint8_t)(rd16(a + 10u + ((uint16_t)(i / 2u) * 2u)) >> ((i & 1u) ? 8u : 0u));
    if (c < 0x20u || c > 0x7Eu) return 0u;
  }
  if (seq_out) *seq_out = seq;
  return 1u;
}

static uint16_t page_valid_count(uint16_t page)
{
  uint16_t n = 0u;
  for (uint16_t s = 0u; s < PKTLOG_SLOTS_PER_PAGE; s++) {
    if (slot_probe(slot_addr_of(page, s), 0)) n++;
  }
  return n;
}

static uint8_t page_blank(uint16_t page)
{
  for (uint16_t s = 0u; s < PKTLOG_SLOTS_PER_PAGE; s++) {
    if (rd16(slot_addr_of(page, s)) != PKTLOG_EMPTY) return 0u;
  }
  return 1u;
}

static void flash_unlock(void)
{
#ifndef PKTLOG_HOST
  HAL_FLASH_Unlock();
#endif
}

static void flash_lock(void)
{
#ifndef PKTLOG_HOST
  HAL_FLASH_Lock();
#endif
}

/* 写一个半字：真机走 HAL，主机回归走内存数组（重复写非空半字会被告警） */
static uint8_t flash_program_half(uint32_t addr, uint16_t val)
{
#ifdef PKTLOG_HOST
  return pktlog_host_program(addr, val);
#else
  return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, (uint64_t)val) == HAL_OK) ? 1u : 0u;
#endif
}

static uint8_t page_erase(uint16_t page)
{
#ifdef PKTLOG_HOST
  pktlog_host_erase(s_base + ((uint32_t)page * PKTLOG_PAGE_BYTES));
  return 1u;
#else
  FLASH_EraseInitTypeDef ei;
  uint32_t page_err = 0u;
  HAL_StatusTypeDef st;
  ei.TypeErase   = FLASH_TYPEERASE_PAGES;
  ei.PageAddress = s_base + ((uint32_t)page * PKTLOG_PAGE_BYTES);
  ei.NbPages     = 1u;
  flash_unlock();
  st = HAL_FLASHEx_Erase(&ei, &page_err);
  flash_lock();
  hw_watchdog_feed();     /* 页擦除 20~40ms，喂一次狗（看门狗 2s，纯保险） */
  return (st == HAL_OK) ? 1u : 0u;
#endif
}

static void rec_encode(const pktlog_rec_t *r, uint16_t *w)
{
  uint8_t meta = 0u;
  if (r->time_kind)     meta |= 0x01u;
  if (r->flags & 0x01u) meta |= 0x02u;
  if (r->flags & 0x02u) meta |= 0x04u;
  if (r->have_rf)       meta |= 0x08u;
  if (r->info_len > PKTLOG_INFO_MAX) meta |= 0x10u;

  w[0] = 0u;                                             /* seq：追加时最后写（提交标记） */
  w[1] = (uint16_t)(r->t & 0xFFFFu);
  w[2] = (uint16_t)(r->t >> 16);
  w[3] = (uint16_t)((uint16_t)meta | ((uint16_t)r->rssi << 8));
  w[4] = (uint16_t)((uint16_t)r->snr | ((uint16_t)r->info_len << 8));
  for (uint8_t i = 0u; i < 6u; i += 2u) {
    w[5u + (uint16_t)(i / 2u)] = (uint16_t)((uint8_t)r->call[i] |
                                   ((uint16_t)(uint8_t)r->call[i + 1u] << 8));
  }
  w[8] = r->hash;
  w[9] = (uint16_t)((uint16_t)(r->ssid & 0x0Fu) | ((uint16_t)PKTLOG_MAGIC << 8));
  for (uint8_t i = 0u; i < PKTLOG_INFO_MAX; i += 2u) {
    uint16_t lo = ((uint16_t)i < (uint16_t)r->info_len) ? (uint8_t)r->info[i] : 0u;
    uint16_t hi = ((uint16_t)(i + 1u) < (uint16_t)r->info_len) ? (uint8_t)r->info[i + 1u] : 0u;
    w[10u + (uint16_t)(i / 2u)] = (uint16_t)(lo | (hi << 8));
  }
}

static void rec_decode(const uint16_t *w, pktlog_rec_t *r)
{
  uint8_t meta = (uint8_t)(w[3] & 0x00FFu);
  r->t         = (uint32_t)w[1] | ((uint32_t)w[2] << 16);
  r->rssi      = (uint8_t)(w[3] >> 8);
  r->snr       = (uint8_t)(w[4] & 0x00FFu);
  r->info_len  = (uint8_t)(w[4] >> 8);
  r->time_kind = (meta & 0x01u) ? 1u : 0u;
  r->flags     = (uint8_t)(((meta & 0x02u) ? 1u : 0u) | ((meta & 0x04u) ? 2u : 0u));
  r->have_rf   = (meta & 0x08u) ? 1u : 0u;
  r->hash      = w[8];
  for (uint8_t i = 0u; i < 6u; i++) {
    r->call[i] = (char)(uint8_t)(w[5u + (uint16_t)(i / 2u)] >> ((i & 1u) ? 8u : 0u));
  }
  r->ssid = (uint8_t)(w[9] & 0x00FFu);
  for (uint8_t i = 0u; i < PKTLOG_INFO_MAX; i++) {
    r->info[i] = (char)(uint8_t)(w[10u + (uint16_t)(i / 2u)] >> ((i & 1u) ? 8u : 0u));
  }
}

static uint8_t rec_load(uint16_t idx, pktlog_rec_t *r)
{
  uint32_t a = idx_addr(idx);
  uint16_t w[PKTLOG_WORDS];

  if (!slot_probe(a, 0)) return 0u;
  for (uint16_t i = 0u; i < PKTLOG_WORDS; i++) w[i] = rd16(a + ((uint32_t)i * 2u));
  rec_decode(w, r);
  return 1u;
}

void pktlog_init(void)
{
  uint16_t newest_idx = 0u, newest_seq = 0u, n = 0u;
  uint8_t  have = 0u;

  s_ready = 0u; s_count = 0u; s_seq_next = 0u; s_page = 0u; s_slot = 0u;

#ifdef PKTLOG_HOST
  s_base  = PKTLOG_HOST_BASE;                       /* 主机回归：内存数组里的固定区间 */
  s_pages = PKTLOG_HOST_PAGES;
#else
  {
    extern uint32_t _image_end;                     /* 链接脚本导出：固件映像在 Flash 里的结束地址 */
    uint32_t base  = ((uint32_t)&_image_end + (PKTLOG_PAGE_BYTES - 1u)) & ~(PKTLOG_PAGE_BYTES - 1u);
    uint32_t pages = (PKTLOG_FLASH_TOP - base) / PKTLOG_PAGE_BYTES;
    if (pages > (uint32_t)BBCALL_PKTLOG_PAGES) pages = (uint32_t)BBCALL_PKTLOG_PAGES;
    s_base  = base;
    s_pages = (uint16_t)pages;
  }
#endif
  s_cap = (uint16_t)(s_pages * PKTLOG_SLOTS_PER_PAGE);
  if (s_cap == 0u) return;                          /* 没有剩余页：日志功能停用（正常固件不会走到这） */

  for (uint16_t idx = 0u; idx < s_cap; idx++) {
    uint16_t seq;
    if (!slot_probe(idx_addr(idx), &seq)) continue;
    n++;
    if (!have) { have = 1u; newest_seq = seq; newest_idx = idx; }
    else {
      /* 环上序号是连续的，跨度远小于 32768：小的一半就是"更新" */
      if ((uint16_t)(seq - newest_seq) < (uint16_t)(newest_seq - seq)) {
        newest_seq = seq; newest_idx = idx;
      }
    }
  }

  s_count = n;
  if (have) {
    s_seq_next = (uint16_t)(newest_seq + 1u);
    s_page = (uint16_t)(newest_idx / PKTLOG_SLOTS_PER_PAGE);
    s_slot = (uint16_t)((newest_idx % PKTLOG_SLOTS_PER_PAGE) + 1u);
    if (s_slot < PKTLOG_SLOTS_PER_PAGE) {
      /* 上次写这条时掉电（seq 没写成但内容已动过）：本页剩余槽作废，下一条开新页 */
      uint32_t a = slot_addr_of(s_page, s_slot);
      if (rd16(a) != PKTLOG_EMPTY || rd16(a + 2u) != PKTLOG_EMPTY) s_slot = PKTLOG_SLOTS_PER_PAGE;
    }
  } else {
    s_seq_next = 0u;
    if (!page_blank(0u)) (void)page_erase(0u);   /* 区里是旧固件残字节：先清干净，别让它们冒充记录 */
  }
  s_ready = 1u;
}

uint8_t pktlog_append(const pktlog_rec_t *r)
{
  uint16_t w[PKTLOG_WORDS];
  uint32_t addr;
  uint8_t  ok = 1u;

  if (!s_ready || !r) return 0u;

  if (s_slot >= PKTLOG_SLOTS_PER_PAGE) {
    /* 当前页写满：前进一页，擦掉的这一页装的是最旧数据 */
    uint16_t nxt = (uint16_t)((s_page + 1u) % s_pages);
    uint16_t old = page_valid_count(nxt);
    if (!page_erase(nxt)) return 0u;
    s_page = nxt;
    s_slot = 0u;
    s_count = (s_count >= old) ? (uint16_t)(s_count - old) : 0u;
  }

  rec_encode(r, w);
  w[0] = s_seq_next;
  addr = slot_addr_of(s_page, s_slot);

  flash_unlock();
  for (uint16_t i = 1u; i < PKTLOG_WORDS; i++) {        /* 先写内容 */
    if (!flash_program_half(addr + ((uint32_t)i * 2u), w[i])) { ok = 0u; break; }
  }
  if (ok) {                                             /* seq 最后写 = 提交这条记录 */
    if (!flash_program_half(addr, w[0])) ok = 0u;
  }
  flash_lock();

  if (!ok) return 0u;
  s_slot++;
  if (s_count < s_cap) s_count++;
  s_seq_next++;
  return 1u;
}

uint16_t pktlog_count(void)     { return s_count; }
uint16_t pktlog_capacity(void)  { return s_cap; }
uint16_t pktlog_pages(void)     { return s_pages; }
uint32_t pktlog_base(void)      { return s_base; }
uint16_t pktlog_seq_last(void)  { return s_seq_next ? (uint16_t)(s_seq_next - 1u) : 0u; }

static void dump_item(uint16_t i, uint16_t n, const pktlog_rec_t *r)
{
  uint8_t clen = 6u;

  while (clen > 0u && r->call[clen - 1u] == ' ') clen--;   /* 呼号末尾空格不打印 */

  hw_console_puts("[LOG] i="); hw_console_u16(i);
  hw_console_putc('/');        hw_console_u16(n);
  hw_console_puts(" t=");
  if (r->time_kind == 0u) {
    rtc_dt_t dt;
    char buf[20];
    rtc_epoch2000_to_dt(r->t, &dt);
    rtc_format_dt(&dt, buf, (uint8_t)sizeof(buf));
    hw_console_puts(buf);
  } else {
    hw_console_puts("UP+"); hw_console_u32(r->t); hw_console_putc('s');
  }
  hw_console_puts(" src=");
  for (uint8_t k = 0u; k < clen; k++) hw_console_putc(r->call[k]);
  if (r->ssid) { hw_console_putc('-'); hw_console_u16((uint16_t)r->ssid); }
  hw_console_puts(" rssi=");
  if (r->have_rf) hw_console_u16((uint16_t)r->rssi); else hw_console_puts("--");
  hw_console_puts(" snr=");
  if (r->have_rf) hw_console_u16((uint16_t)r->snr); else hw_console_puts("--");
  hw_console_puts(" f=");
  if (r->flags & 0x02u)      hw_console_puts("REP");
  else if (r->flags & 0x01u) hw_console_puts("FIX");
  else                       hw_console_puts("OK");
  hw_console_puts(" h="); hw_console_hex16(r->hash);
  hw_console_puts(" info=");
  for (uint8_t k = 0u; k < r->info_len; k++) {
    char c = r->info[k];
    if ((uint8_t)c < 0x20u || c == 0x7F) c = '.';   /* 控制字符替换，保证一行一条、可解析 */
    hw_console_putc(c);
  }
  hw_console_puts("\r\n");
}

void pktlog_dump(void)
{
  uint16_t n, oldest, seq_last;
  uint32_t lost;

  if (!s_ready) { hw_console_puts("[LOG] not ready\r\n"); return; }

  n = s_count;
  seq_last = n ? (uint16_t)(s_seq_next - 1u) : 0u;
  lost = ((uint32_t)s_seq_next >= (uint32_t)n) ? ((uint32_t)s_seq_next - (uint32_t)n) : 0u;

  hw_console_puts("[LOG] n=");   hw_console_u16(n);
  hw_console_puts(" cap=");      hw_console_u16(s_cap);
  hw_console_puts(" seq_last="); hw_console_u16(seq_last);
  hw_console_puts(" lost=");     hw_console_u32(lost);
  hw_console_puts(" order=old2new\r\n");

  oldest = (uint16_t)(s_seq_next - n);
  for (uint16_t k = 0u; k < n; k++) {
    uint16_t want = (uint16_t)(oldest + k);
    pktlog_rec_t rec;
    for (uint16_t idx = 0u; idx < s_cap; idx++) {
      uint16_t seq;
      if (!slot_probe(idx_addr(idx), &seq) || seq != want) continue;
      if (rec_load(idx, &rec)) dump_item((uint16_t)(k + 1u), n, &rec);
      break;
    }
    if ((k & 7u) == 7u) hw_watchdog_feed();   /* 112 行约 1s 的串口时间，喂狗别让它咬 */
  }
}

void pktlog_clear(void)
{
  uint16_t old_n = s_count;
  if (!s_ready) return;
  for (uint16_t p = 0u; p < s_pages; p++) (void)page_erase(p);
  s_count = 0u; s_seq_next = 0u; s_page = 0u; s_slot = 0u;
  hw_console_puts("[LOG] erased n="); hw_console_u16(old_n);
  hw_console_puts(" -> 0\r\n");
}