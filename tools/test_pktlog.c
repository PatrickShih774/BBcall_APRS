/* tools/test_pktlog.c：设备端解码日志（pktlog.c）的主机回归测试。
 *
 * 不用烧板子：PKTLOG_HOST 把内部 Flash 换成 64KB 内存数组，
 * 重点验证"环形写入 + 掉电半条 + 开机扫描定位写指针 + 垃圾区容错"这些真机上不好复现的分支。
 *
 * 编译运行（仓库根目录，TinyCC）：
 *   third_party\tcc\tcc\tcc.exe -DPKTLOG_HOST -I firmware-stm32porject\Core\Inc ^
 *     -o %TEMP%\pktlog.exe tools\test_pktlog.c firmware-stm32porject\Core\Src\pktlog.c ^
 *     firmware-stm32porject\Core\Src\rtc_math.c firmware-stm32porject\Core\Src\strfmt.c
 *   %TEMP%\pktlog.exe
 * 期望输出最后一行：PKTLOG TEST: all pass */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pktlog.h"

/* ---------- 内存 Flash 模型（0x08000000 起 64KB，页 1KB） ---------- */
static uint8_t g_flash[65536];
static int     g_double;          /* 重复写同一半字的次数（真 Flash 会写坏，必须为 0） */
static long    g_programs, g_erases;
static char    g_out[1 << 18];
static uint32_t g_last_prog_addr;   /* 最近一次写半字的地址（seq 最后写，所以是刚写那条的槽基址） */
static size_t  g_len;

static void add(const char *s) { while (*s) g_out[g_len++] = *s++; }
static void num(unsigned long v)
{
  char b[24]; int i = 0;
  if (!v) { g_out[g_len++] = '0'; return; }
  while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; }
  while (i) g_out[g_len++] = b[--i];
}
void hw_console_puts(const char *s) { add(s); }
void hw_console_putc(char c) { g_out[g_len++] = c; }
void hw_console_u8(uint8_t v) { num(v); }
void hw_console_u16(uint16_t v) { num(v); }
void hw_console_u32(uint32_t v) { num(v); }
void hw_console_hex8(uint8_t v) { char b[4]; sprintf(b, "%02X", v); add(b); }
void hw_console_hex16(uint16_t v) { char b[6]; sprintf(b, "%04X", v); add(b); }
void hw_watchdog_feed(void) { }

static uint16_t rd16_raw(uint32_t a) { uint32_t i = a & 0xFFFFu; return (uint16_t)(g_flash[i] | ((uint16_t)g_flash[i + 1] << 8)); }
uint16_t pktlog_host_rd16(uint32_t addr) { return rd16_raw(addr); }
uint8_t pktlog_host_program(uint32_t addr, uint16_t val)
{
  uint32_t i = addr & 0xFFFFu;
  uint16_t old = rd16_raw(addr);
  if (old != 0xFFFFu) {                       /* 真 Flash 只能 1->0，重复写会损坏数据 */
    printf("  [warn] double-program @%08X old=%04X new=%04X\n", (unsigned)addr, old, val);
    g_double++;
  }
  uint16_t nv = (uint16_t)(old & val);
  g_flash[i] = (uint8_t)(nv & 0xFFu);
  g_flash[i + 1] = (uint8_t)(nv >> 8);
  g_programs++;
  g_last_prog_addr = addr;
  return 1u;
}
void pktlog_host_erase(uint32_t addr) { memset(&g_flash[addr & 0xFFFFu], 0xFF, 1024); g_erases++; }

/* ---------- 断言 ---------- */
static int g_fail;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail++; } } while (0)

/* ---------- dump 输出解析 ---------- */
static int g_items;

static void scan_dump(int *n, int *cap, int *seq_last, int *lost)
{
  const char *p = g_out;
  *n = *cap = *seq_last = *lost = -1;
  g_items = 0;
  while (*p) {
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    while (len && (p[len - 1] == '\r' || p[len - 1] == '\n')) len--;
    if (len > 8u && strncmp(p, "[LOG] n=", 8u) == 0) {
      sscanf(p, "[LOG] n=%d cap=%d seq_last=%d lost=%d", n, cap, seq_last, lost);
    } else if (len > 8u && strncmp(p, "[LOG] i=", 8u) == 0) {
      g_items++;
    }
    if (!e) break;
    p = e + 1;
  }
}

/* 取第 k 条（1 基）某个字段：field 形如 "info=" / "src=" */
static int item_field(int k, const char *field, char *dst, size_t cap)
{
  const char *p = g_out;
  int idx = 0;
  size_t fl = strlen(field);
  while ((p = strstr(p, "[LOG] i=")) != NULL) {
    const char *line_end = strchr(p, '\n');
    idx++;
    if (idx == k) {
      const char *q = strstr(p, field);
      if (!q || (line_end && q > line_end)) return 0;
      q += fl;
      size_t l = line_end ? (size_t)(line_end - q) : strlen(q);
      while (l && (q[l - 1] == '\r' || q[l - 1] == '\n')) l--;
      if (l >= cap) l = cap - 1;
      memcpy(dst, q, l); dst[l] = 0;
      return 1;
    }
    p += 8;
  }
  return 0;
}

/* 取第 k 条第 field 个字段的单个 token（到下一个空格为止），用于 src=/rssi=/snr=/f=/h= */
static int item_token(int k, const char *field, char *dst, size_t cap)
{
  char tmp[256];
  size_t i = 0;
  if (!item_field(k, field, tmp, sizeof(tmp))) return 0;
  while (tmp[i] && tmp[i] != ' ' && i + 1u < cap) { dst[i] = tmp[i]; i++; }
  dst[i] = 0;
  return 1;
}

static void mk(pktlog_rec_t *r, const char *call, uint8_t ssid, unsigned n)
{
  memset(r, 0, sizeof(*r));
  r->t = 400000000u + n;                       /* 2000-01-01 起秒数 -> 2012-09-08 前后 */
  r->time_kind = 0u;
  r->flags = (uint8_t)(n % 3u == 0u ? 1u : (n % 3u == 1u ? 2u : 0u));  /* FIX / REP / OK 轮流 */
  r->have_rf = 1u;
  r->rssi = (uint8_t)(60u + n % 40u);
  r->snr  = (uint8_t)(n % 40u);
  r->hash = (uint16_t)(0x1000u + n);
  memcpy(r->call, call, 6);
  r->ssid = ssid;
  snprintf(r->info, PKTLOG_INFO_MAX + 1, "PKT %06u", n);
  r->info_len = (uint8_t)strlen(r->info);
}

/* 每次 dump 后的自洽校验：header 与明细行数、首末内容、序号连续性 */
static void check_consistent(const char *tag)
{
  int n, cap, seq_last, lost;
  char a[80], b[80], msg[128];
  scan_dump(&n, &cap, &seq_last, &lost);
  sprintf(msg, "%s: header n=%d != 明细行数 %d", tag, n, g_items);
  CHECK(n == g_items, msg);
  sprintf(msg, "%s: cap=%d != %d", tag, cap, (int)pktlog_capacity());
  CHECK(cap == (int)pktlog_capacity(), msg);
  sprintf(msg, "%s: count=%d != n=%d", tag, (int)pktlog_count(), n);
  CHECK((int)pktlog_count() == n, msg);
  sprintf(msg, "%s: lost=%d != seq_last+1-n=%d", tag, lost, seq_last + 1 - n);
  CHECK(lost == seq_last + 1 - n, msg);
  if (n > 0) {
    char exp[80];
    int hdr_ok = 0;
    /* 明细顺序必须是旧 -> 新，且序号连续（用 info 里的序号判断） */
    for (int k = 1; k <= n; k++) {
      unsigned v = 0;
      if (!item_field(k, "info=", a, sizeof(a))) { sprintf(msg, "%s: 第 %d 条解析不到 info=", tag, k); CHECK(0, msg); hdr_ok = 1; break; }
      if (sscanf(a, "PKT %u", &v) != 1) { sprintf(msg, "%s: 第 %d 条 info 不是 PKT 序号：%s", tag, k, a); CHECK(0, msg); hdr_ok = 1; break; }
      snprintf(exp, sizeof(exp), "PKT %06u", (unsigned)(seq_last + 1 - n + (k - 1)));
      if (strcmp(a, exp) != 0) { sprintf(msg, "%s: 第 %d 条 = %s，应为 %s", tag, k, a, exp); CHECK(0, msg); hdr_ok = 1; break; }
    }
    if (hdr_ok) { /* 上面已记 FAIL */ }
    CHECK(item_token(1, "h=", b, sizeof(b)) && strlen(b) == 4u, "哈希 4 位");
  }
}

int main(void)
{
  pktlog_rec_t r;
  char buf[128];
  int i, n, cap, seq_last, lost;

  printf("CASE 1: 空白 Flash 开机\n");
  memset(g_flash, 0xFF, sizeof(g_flash));
  pktlog_init();
  CHECK(pktlog_count() == 0u, "count==0");
  CHECK(pktlog_capacity() == 112u, "cap==112");
  CHECK(pktlog_seq_last() == 0u, "seq_last==0");
  g_len = 0; pktlog_dump(); g_out[g_len] = 0;
  scan_dump(&n, &cap, &seq_last, &lost);
  CHECK(n == 0 && cap == 112, "[LOG] n=0 cap=112");

  printf("CASE 2: 追加 5 条，字段回读\n");
  for (i = 0; i < 5; i++) { mk(&r, "BG5BLH", 12u, (unsigned)i); CHECK(pktlog_append(&r) == 1u, "append"); }
  g_len = 0; pktlog_dump(); g_out[g_len] = 0;
  check_consistent("追加5");
  CHECK(item_token(1, "src=", buf, sizeof(buf)) && strcmp(buf, "BG5BLH-12") == 0, "src=BG5BLH-12");
  CHECK(item_token(1, "rssi=", buf, sizeof(buf)) && strcmp(buf, "60") == 0, "rssi 回读");
  CHECK(item_token(1, "snr=", buf, sizeof(buf)) && strcmp(buf, "0") == 0, "snr 回读");
  CHECK(item_token(1, "f=", buf, sizeof(buf)) && strcmp(buf, "FIX") == 0, "f=FIX");
  CHECK(item_token(1, "h=", buf, sizeof(buf)) && strcmp(buf, "1000") == 0, "h=1000");
  CHECK(strstr(g_out, "t=2012-09-") != NULL, "RTC 时间戳格式 t=2012-09-..");

  printf("CASE 3: 回绕 300 条\n");
  for (i = 5; i < 300; i++) { mk(&r, "BG5BLH", 12u, (unsigned)i); pktlog_append(&r); }
  CHECK(pktlog_count() >= 99u && pktlog_count() <= 112u, "回绕后条数在 99..112（页擦除粒度）");
  g_len = 0; pktlog_dump(); g_out[g_len] = 0;
  check_consistent("回绕300");
  scan_dump(&n, &cap, &seq_last, &lost);
  CHECK(seq_last == 299, "seq_last==299");
  CHECK(item_field(n, "info=", buf, sizeof(buf)) && strcmp(buf, "PKT 000299") == 0, "最后一条=PKT 000299");

  printf("CASE 4: 断电重启后续写\n");
  { int c0 = (int)pktlog_count();
    pktlog_init();
    CHECK((int)pktlog_count() == c0, "count 复位后不变");
    mk(&r, "BG5BLH", 12u, 300u); CHECK(pktlog_append(&r) == 1u, "续写 append");
    g_len = 0; pktlog_dump(); g_out[g_len] = 0;
    check_consistent("续写");
    scan_dump(&n, &cap, &seq_last, &lost);
    CHECK(seq_last == 300, "seq_last==300");
    CHECK(item_field(n, "info=", buf, sizeof(buf)) && strcmp(buf, "PKT 000300") == 0, "续写后最后一条");
  }

  printf("CASE 5: 掉电半条（seq 没写成）\n");
  { int before = g_double;
    uint32_t slot_addr;
    mk(&r, "BG5BLH", 12u, 301u);
    pktlog_append(&r);
    slot_addr = g_last_prog_addr;                /* seq 半字最后写：这个地址就是槽基址 */
    CHECK(rd16_raw(slot_addr) == 301u, "最后写的是这条的 seq=301");
    g_flash[slot_addr & 0xFFFFu] = 0xFF;         /* 内容还在，但 seq 没写成（掉电半条） */
    g_flash[(slot_addr + 1u) & 0xFFFFu] = 0xFF;
    CHECK(rd16_raw(slot_addr) == 0xFFFFu, "seq 已被清成 0xFFFF");
    pktlog_init();
    mk(&r, "BG5BLH", 12u, 301u);                 /* re-init 后 seq_next 仍是 301 */
    CHECK(pktlog_append(&r) == 1u, "半条之后仍能写");
    g_len = 0; pktlog_dump(); g_out[g_len] = 0;
    check_consistent("掉电半条");
    scan_dump(&n, &cap, &seq_last, &lost);
    CHECK(item_field(n, "info=", buf, sizeof(buf)) && strcmp(buf, "PKT 000301") == 0, "半条后最后一条=PKT 000301");
    CHECK(g_double == before, "没有重复写半字");
  }

  printf("CASE 6: 整片垃圾（旧固件残字节）容错\n");
  { unsigned int x = 12345u;
    for (i = 0; i < 65536; i++) { x = x * 1103515245u + 12345u; g_flash[i] = (uint8_t)(x >> 16); }
    pktlog_init();
    CHECK(pktlog_count() == 0u, "垃圾全部被拒");
    mk(&r, "BG5BLH", 12u, 0u); CHECK(pktlog_append(&r) == 1u, "垃圾区上写入");
    mk(&r, "BG5BLH", 12u, 1u); pktlog_append(&r);
    g_len = 0; pktlog_dump(); g_out[g_len] = 0;
    check_consistent("垃圾区");
    CHECK(g_double == 0, "垃圾区没有重复写半字");
  }

  printf("CASE 7: 清空\n");
  pktlog_clear();
  CHECK(pktlog_count() == 0u, "clear 后 count==0");
  g_len = 0; pktlog_dump(); g_out[g_len] = 0;
  scan_dump(&n, &cap, &seq_last, &lost);
  CHECK(n == 0 && seq_last == 0, "clear 后 dump n=0");

  printf("CASE 8: 打印格式边界\n");
  mk(&r, "BG5BLH", 12u, 0u); pktlog_append(&r);
  mk(&r, "BG5BLH", 0u, 1u);                 /* ssid=0：不打印 -0 */
  r.have_rf = 0u;                           /* 没有 RSSI/SNR 采样：打 -- */
  pktlog_append(&r);
  mk(&r, "BG5BLH", 1u, 2u); r.time_kind = 1u; r.t = 1234u;   /* RTC 没对时：UP+秒数 */
  pktlog_append(&r);
  g_len = 0; pktlog_dump(); g_out[g_len] = 0;
  CHECK(strstr(g_out, " rssi=-- snr=-- f=") != NULL, "无采样打 --");
  CHECK(strstr(g_out, " src=BG5BLH rssi=") != NULL, "ssid=0 不打 -0");
  CHECK(strstr(g_out, " t=UP+1234s src=BG5BLH-1 ") != NULL, "UP+秒数时间戳");
  CHECK(item_token(1, "h=", buf, sizeof(buf)) && strlen(buf) == 4u, "哈希 4 位十六进制");
  check_consistent("格式边界");

  printf("  擦除 %ld 次、写半字 %ld 次、重复写 %d 次\n", g_erases, g_programs, g_double);
  printf("\nPKTLOG TEST: %s\n", g_fail ? "FAIL" : "all pass");
  return g_fail ? 1 : 0;
}