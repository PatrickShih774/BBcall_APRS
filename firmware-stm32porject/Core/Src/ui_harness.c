/*
 * BBcall_APRS UI -- design.md v2.0 三态模型（2026-09-16 重写）
 *
 *   态 1 待机 UI_SCREEN_IDLE     无未读时停留；大格 = 时钟
 *   态 2 有未读 UI_SCREEN_UNREAD 收包后大格 = 最新一条未读
 *   态 3 收件箱 UI_SCREEN_INBOX  通栏骨架：顶栏(反显) + 正文 + 元信息
 *
 * 版面坐标逐像素对应 bbcall-aprs-screen-states.html 原型（design.md §5/§6）。
 * 字模：Fusion Pixel 12px/10px（fusion_font.h，tools/gen_fusion_font.py 生成）。
 * 按键只有 ▲ ▼ ●（长按 620ms 退出收件箱）；态 1/2 的 ▲▼ 是死键，屏幕无反应。
 *
 * 数据诚实（design.md §8.5/§12）：
 *   - 无 RTC 时态 1 大格第二行显示开机时长（UP Nd HH:MM），不编造绝对日期；
 *     有 RTC 时经 ui_set_wallclock() 显示 周三 9/16。
 *   - 无 RSSI/SNR 采样时显示 --，不编造数值；模拟器由 ui_set_radio_stats 注入。
 *   - RSSI/SNR 随帧入箱时捕获，属于该帧，不是全局状态。
 */
#include "ui_harness.h"
#include "lcd_st7567.h"
#include "fusion_font.h"
#include "ax25.h"
#include "aprs.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 收件箱条目                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
  uint8_t  used;
  uint16_t hash;       /* 整帧哈希：重复包靠它找到原条目刷新时间戳 */
  uint8_t  read;
  uint8_t  kind;        /* UI_KIND_*（详情元信息用；列表不再显示类型字母） */
  uint8_t  fixed;       /* CRC 级别：FIX（FIX2 回放路径无法区分，先并入 FIX） */
  uint8_t  repeat;      /* CRC 级别：REP */
  uint8_t  have_pos;    /* 上格显示经纬度；否则显示 dst/path */
  uint8_t  have_rf;     /* rssi/snr 有真实采样 */
  int16_t  rssi;
  int16_t  snr;
  char     src[12];    /* 呼号 + "-SSID"：BG5BLB-12 是 10 字符，留 12 字节 */
  char     dst[10];
  char     path[22];    /* 中继路径：屏上一列约 10 格，22 字节足够 */
  char     lat[10];
  char     lon[11];
  char     body[UI_BODY_MAX];   /* UTF-8 */
  uint32_t rx_ms;
} ui_item_t;

#define UI_KIND_MSG   0
#define UI_KIND_POS   1
#define UI_KIND_MICE  2
#define UI_KIND_OTHER 3

static ui_item_t s_box[UI_INBOX_MAX];
static uint8_t   s_count, s_idx, s_vscroll;
static uint8_t   s_view = UI_SCREEN_IDLE;   /* 状态机当前态（= 屏幕编号 1/2/3） */
static uint32_t  s_now_ms;                  /* 设备时钟（uptime；--clock 固定） */
static uint8_t   s_wday = 0xFFu, s_mon, s_mday;  /* 0xFF = 无 RTC */
static int8_t    s_batt = -1;               /* -1 无采样 */
static char      s_mycall[10] = "NOCALL";
static uint32_t  s_freq_khz = 144640u;
static int16_t   s_rssi_next = 0, s_snr_next = 0;
static uint8_t   s_have_rf_next = 0u;
static uint16_t  s_rx_total, s_unread, s_dup_total, s_ack_total;

#define UI_DUP_N 8
static uint16_t s_dup_hash[UI_DUP_N];
static char     s_dup_src[UI_DUP_N][7];
static uint32_t s_dup_ms[UI_DUP_N];
static uint8_t  s_dup_pos;

/* ------------------------------------------------------------------ */
/* UTF-8 与字模渲染（与原型 glyph()/text() 逐位一致）                    */
/* ------------------------------------------------------------------ */
/* 取一个 UTF-8 码位；返回字节数（0 = 非法，按 1 字节跳过） */
static uint8_t utf8_cp(const char *s, uint32_t *cp)
{
  const uint8_t *u = (const uint8_t *)s;
  if (u[0] < 0x80u) { *cp = u[0]; return 1u; }
  if ((u[0] & 0xE0u) == 0xC0u && (u[1] & 0xC0u) == 0x80u) {
    *cp = ((uint32_t)(u[0] & 0x1Fu) << 6) | (uint32_t)(u[1] & 0x3Fu);
    return 2u;
  }
  if ((u[0] & 0xF0u) == 0xE0u && (u[1] & 0xC0u) == 0x80u && (u[2] & 0xC0u) == 0x80u) {
    *cp = ((uint32_t)(u[0] & 0x0Fu) << 12) | ((uint32_t)(u[1] & 0x3Fu) << 6) |
          (uint32_t)(u[2] & 0x3Fu);
    return 3u;
  }
  *cp = u[0];
  return 1u;
}

static uint8_t cp_wide(uint32_t cp) { return cp >= 0x2E80u; }

/* 码位 -> 元数据（找不到返回 NULL：不绘制但仍占推进宽度，与原型一致） */
static const fp_glyph_meta_t *fp_lookup(uint32_t cp, uint8_t tier)
{
  if (cp >= 0x20u && cp <= 0x7Eu) {
    return (tier == 12u) ? &fp12_ascii_meta[cp - 0x20u] : &fp10_ascii_meta[cp - 0x20u];
  } else {
    const uint16_t *codes = (tier == 12u) ? fp12_cjk_code : fp10_cjk_code;
    const fp_glyph_meta_t *meta = (tier == 12u) ? fp12_cjk_meta : fp10_cjk_meta;
    uint16_t n = (tier == 12u) ? FP12_CJK_N : FP10_CJK_N;
    int lo = 0, hi = (int)n - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (codes[mid] == cp) return &meta[mid];
      if (codes[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
  }
  return NULL;
}

static const uint8_t *fp_bitmap(uint8_t tier)
{
  return (tier == 12u) ? fp12_bitmap : fp10_bitmap;
}

static void fp_glyph_draw(uint32_t cp, uint8_t tier, int16_t gx, int16_t gy,
                          uint8_t ink, uint8_t k)
{
  const fp_glyph_meta_t *g = fp_lookup(cp, tier);
  const uint8_t *bm = fp_bitmap(tier);
  uint8_t r, b;
  if (!g || g->gw == 0u || g->rows == 0u) return;
  for (r = 0u; r < g->rows; r++) {
    /* 行位图右对齐保存（高位在左的整字节序），像素 b 的测试位是 gw-1-b，
     * 与原型 glyph() 的 v & (1 << (gw-1-b)) 逐位一致 */
    uint16_t u = 0;
    uint8_t j;
    for (j = 0u; j < g->rb; j++)
      u = (uint16_t)((u << 8) | bm[g->off + (uint16_t)r * g->rb + j]);
    for (b = 0u; b < g->gw; b++) {
      if ((u >> (g->gw - 1u - b)) & 1u) {
        int16_t px = (int16_t)(gx + (int16_t)(g->ox + b) * k);
        int16_t py = (int16_t)(gy + (int16_t)(g->oy + r) * k);
        if (k == 1u) lcd_pixel((uint8_t)px, (uint8_t)py, ink);
        else lcd_fill_rect((uint8_t)px, (uint8_t)py,
                           (uint8_t)(px + (int16_t)k - 1), (uint8_t)(py + (int16_t)k - 1), ink);
      }
    }
  }
}

/* 单元格推进宽度（像素）：倍率只放字形，不放落点（design.md §3.2） */
static uint8_t cell_adv(uint32_t cp, uint8_t tier, uint8_t k)
{
  uint8_t a = (tier == 12u) ? FP12_CELL_A : FP10_CELL_A;
  uint8_t w = (tier == 12u) ? FP12_CELL_W : FP10_CELL_W;
  return (uint8_t)((cp_wide(cp) ? w : a) * k);
}

/* 字符串总宽（像素） */
static uint16_t fp_width(const char *s, uint8_t tier, uint8_t k)
{
  uint16_t w = 0u;
  while (*s) {
    uint32_t cp;
    uint8_t n = utf8_cp(s, &cp);
    w = (uint16_t)(w + cell_adv(cp, tier, k));
    s += n;
  }
  return w;
}

static void fp_text(int16_t x, int16_t y, const char *s, uint8_t tier, uint8_t ink, uint8_t k)
{
  int16_t cur = x;
  while (*s) {
    uint32_t cp;
    uint8_t n = utf8_cp(s, &cp);
    fp_glyph_draw(cp, tier, cur, y, ink, k);
    cur = (int16_t)(cur + cell_adv(cp, tier, k));
    s += n;
  }
}

/* 在 [x0, x1] 居中（design.md §3.2 公式） */
static int16_t fp_cx(const char *s, uint8_t tier, int16_t x0, int16_t x1, uint8_t k)
{
  uint16_t w = fp_width(s, tier, k);
  return (int16_t)(x0 + (int16_t)((x1 - x0 + 1 - (int16_t)w) / 2));
}

/* ------------------------------------------------------------------ */
/* 按单元格折行（ASCII=1 格，汉字=2 格；不切断 UTF-8 序列）               */
/* ------------------------------------------------------------------ */
#define WRAP_MAXLINES 4     /* 正文上限 64 字节最多 3 行（21 格/行），4 行留滚动余量 */
#define WRAP_MAXCELLS 21

static uint8_t wrap_cells(const char *s, uint8_t maxcells,
                          char lines[][WRAP_MAXCELLS * 3 + 1], uint8_t maxlines)
{
  uint8_t n = 0u;
  while (*s && n < maxlines) {
    uint8_t cells = 0u, bytes = 0u, sp_byte = 0u, sp_cells = 0u;
    while (*s == ' ') s++;               /* 行首空格跳过（与原型预分行一致） */
    if (!*s) break;
    while (s[bytes] && cells < maxcells) {
      uint32_t cp;
      uint8_t adv = utf8_cp(s + bytes, &cp);
      uint8_t c = cp_wide(cp) ? 2u : 1u;
      if (cp == 0x20u) { sp_byte = bytes; sp_cells = cells; }  /* 记录空格位置（不含） */
      cells = (uint8_t)(cells + c);
      bytes = (uint8_t)(bytes + adv);
      if (cells > maxcells) { bytes = (uint8_t)(bytes - adv); cells = (uint8_t)(cells - c); break; }
    }
    /* 本行已满且后面还有内容：在最后一个空格处断行（空格本身留在下行行首，随后被跳过） */
    if (s[bytes] && sp_byte > 0u) { bytes = sp_byte; cells = sp_cells; }
    if (bytes == 0u) break;
    memcpy(lines[n], s, bytes);
    lines[n][bytes] = 0;
    s += bytes;
    n++;
  }
  return n;
}

/* ------------------------------------------------------------------ */
/* 时间与标签                                                          */
/* ------------------------------------------------------------------ */
/* 日内时刻（ms）。设备时钟 = uptime；墙上时刻 = uptime % 1 天。
 * 帧接收时刻按 (rx_ms - now_ms) 折算回当时的墙上时刻，回放日志因此可复现。 */
static uint32_t tod_at(uint32_t rx_ms)
{
  int32_t d = (int32_t)(rx_ms - s_now_ms);
  int32_t t = (int32_t)(s_now_ms % 86400000u) + d;
  t %= 86400000;
  if (t < 0) t += 86400000;
  return (uint32_t)t;
}

static void fmt_hhmmss(uint32_t rx_ms, char *buf, uint8_t cap)
{
  uint32_t t = tod_at(rx_ms) / 1000u;
  snprintf(buf, cap, "%02lu:%02lu:%02lu",
           (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u),
           (unsigned long)(t % 60u));
}

/* 正文是 ackNNN 时视为送达确认（协议流量，不进收件箱） */
static int is_ack_body(const char *text)
{
  uint32_t v = 0;
  int n = 0;
  if (!text) return 0;
  if (text[0] != 'a' || text[1] != 'c' || text[2] != 'k') return 0;
  text += 3;
  while (*text >= '0' && *text <= '9') { v = v * 10u + (uint32_t)(*text - '0'); text++; n++; }
  return (n > 0 && v <= 65535u && *text == 0) ? 1 : 0;
}

static const char *crc_label(const ui_item_t *it)
{
  if (it->repeat) return "REP";
  if (it->fixed)  return "FIX";
  return "OK";
}

/* 最新一条未读 = 从队首（最新）找第一个 read==0 */
static const ui_item_t *latest_unread(void)
{
  uint8_t i;
  for (i = 0u; i < s_count; i++)
    if (!s_box[i].read) return &s_box[i];
  return NULL;
}

/* ------------------------------------------------------------------ */
/* 版面骨架常量（design.md §5；分隔线 1px）                              */
/* ------------------------------------------------------------------ */
static void tile_frame(void)
{
  lcd_vline(64, 0, 63, 1);      /* [64, 0, 1, 64] */
  lcd_hline(64, 127, 32, 1);    /* [64, 32, 64, 1] */
  lcd_vline(96, 32, 63, 1);     /* [96, 32, 1, 32] */
}

/* 态 1/2 的右列小格：标签(10px) + 值(12px)，各自居中 */
static void cell_label_value(int16_t x0, int16_t x1, int16_t label_y, int16_t value_y,
                             const char *label, const char *value, uint8_t inv)
{
  fp_text(fp_cx(label, 10, x0, x1, 1), label_y, label, 10, (uint8_t)(inv ? 0u : 1u), 1u);
  fp_text(fp_cx(value, 12, x0, x1, 1), value_y, value, 12, (uint8_t)(inv ? 0u : 1u), 1u);
}

/* ------------------------------------------------------------------ */
/* 态 1 · 待机                                                          */
/* ------------------------------------------------------------------ */
static void draw_idle(void)
{
  char buf[24];
  char clk[8];
  lcd_clear(0);

  /* 大格（恒反显）：时钟 2 倍 + 日期/时长 */
  lcd_fill_rect(0, 0, 63, 63, 1u);
  {
    uint32_t t = (s_now_ms % 86400000u) / 1000u;
    snprintf(clk, sizeof(clk), "%02lu:%02lu",
             (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u));
    if (((s_now_ms / 500u) & 1u) != 0u) clk[2] = ' ';   /* 冒号 0.5s 闪烁（唯一动效） */
    fp_text(fp_cx(clk, 12, 0, 63, 2u), 11, clk, 12, 0u, 2u);
  }
  if (s_wday != 0xFFu) {
    static const char *const WD[7] =
      { "日", "一", "二", "三", "四", "五", "六" };   /* UTF-8 串按字节索引会取到半个汉字 */
    snprintf(buf, sizeof(buf), "周%s %u/%u", WD[s_wday % 7u],
             (unsigned)s_mon, (unsigned)s_mday);
  } else {
    /* 无 RTC：显示开机时长，不编造绝对日期（design.md §8.5/§12.2） */
    uint32_t d = s_now_ms / 86400000u;
    uint32_t t = (s_now_ms % 86400000u) / 1000u;
    if (d > 0u) snprintf(buf, sizeof(buf), "%ud %02lu:%02lu", (unsigned)d,
                         (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u));
    else        snprintf(buf, sizeof(buf), "UP %02lu:%02lu",
                         (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u));
  }
  fp_text(fp_cx(buf, 12, 0, 63, 1u), 41, buf, 12, 0u, 1u);

  /* 上格：本机呼号 */
  cell_label_value(65, 127, 2, 18, "本机", s_mycall, 0u);
  /* 左下：电量三挡（无采样显示 --） */
  cell_label_value(65, 95, 34, 50, "电量",
                   (s_batt < 0) ? "--" : (s_batt == 0) ? "低" : (s_batt == 1) ? "中" : "高", 0u);
  /* 右下：未读数 */
  snprintf(buf, sizeof(buf), "%u", (unsigned)s_unread);
  cell_label_value(97, 127, 34, 50, "未读", buf, 0u);

  tile_frame();
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 态 2 · 有未读                                                        */
/* ------------------------------------------------------------------ */
static void draw_unread(void)
{
  const ui_item_t *it = latest_unread();
  char buf[24];
  char lines[2][WRAP_MAXCELLS * 3 + 1];   /* 这一屏只用 2 行，不必按最大版面开栈（wrap_cells 的列宽固定） */
  uint8_t n, i;
  lcd_clear(0);
  if (!it) { draw_idle(); return; }

  /* 大格（恒反显）：呼号 + 正文两行 + 接收时间 */
  lcd_fill_rect(0, 0, 63, 63, 1u);
  fp_text(fp_cx(it->src, 12, 0, 63, 1u), 2, it->src, 12, 0u, 1u);
  n = wrap_cells(it->body, 10u, lines, 2u);     /* 64px 格 = 10 单元格/行 */
  for (i = 0u; i < 2u; i++) {
    const char *ln = (i < n) ? lines[i] : "";
    fp_text(fp_cx(ln, 12, 0, 63, 1u), (int16_t)(18 + (int16_t)i * 16), ln, 12, 0u, 1u);
  }
  fmt_hhmmss(it->rx_ms, buf, sizeof(buf));
  fp_text(fp_cx(buf, 12, 0, 63, 1u), 50, buf, 12, 0u, 1u);

  /* 上格：位置帧显示经纬度；其它帧显示发给/路径（真实数据，不编造坐标） */
  if (it->have_pos) {
    fp_text(fp_cx(it->lat, 12, 65, 127, 1u), 2, it->lat, 12, 1u, 1u);
    fp_text(fp_cx(it->lon, 12, 65, 127, 1u), 18, it->lon, 12, 1u, 1u);
  } else {
    fp_text(fp_cx(it->dst, 12, 65, 127, 1u), 2, it->dst, 12, 1u, 1u);
    fp_text(fp_cx(it->path, 12, 65, 127, 1u), 18, it->path, 12, 1u, 1u);
  }

  /* 左下/右下：RSSI / SNR（无采样显示 --） */
  if (it->have_rf) snprintf(buf, sizeof(buf), "%d", (int)it->rssi);
  else snprintf(buf, sizeof(buf), "--");
  cell_label_value(65, 95, 34, 50, "RSSI", buf, 0u);
  if (it->have_rf) snprintf(buf, sizeof(buf), "%d", (int)it->snr);
  else snprintf(buf, sizeof(buf), "--");
  cell_label_value(97, 127, 34, 50, "SNR", buf, 0u);

  tile_frame();
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 态 3 · 收件箱（通栏骨架）                                             */
/* ------------------------------------------------------------------ */
#define INBOX_VIS_LINES 2

static void draw_inbox(void)
{
  const ui_item_t *it;
  char buf[28];
  char lines[WRAP_MAXLINES][WRAP_MAXCELLS * 3 + 1];
  uint8_t n, i, show;
  lcd_clear(0);

  /* 顶栏（恒反显）：发件呼号 + 序号 n / N */
  lcd_fill_rect(0, 0, 127, 11, 1u);
  if (s_count == 0u) {
    fp_text(4, 0, "无消息", 12, 0u, 1u);
    fp_text(94, 0, "0 / 0", 12, 0u, 1u);
    lcd_flush();
    return;
  }
  if (s_idx >= s_count) s_idx = (uint8_t)(s_count - 1u);
  it = &s_box[s_idx];
  fp_text(4, 0, it->src, 12, 0u, 1u);
  snprintf(buf, sizeof(buf), "%u / %u", (unsigned)(s_idx + 1u), (unsigned)s_count);
  fp_text(94, 0, buf, 12, 0u, 1u);

  /* 正文带：y=16 起，12px 行高，2 行可见；超长在带内滚动（§6.3） */
  n = wrap_cells(it->body, 21u, lines, WRAP_MAXLINES);
  if (n == 0u) { lines[0][0] = 0; n = 1u; }
  show = (n > INBOX_VIS_LINES) ? INBOX_VIS_LINES : n;
  if (s_vscroll > (uint8_t)(n - INBOX_VIS_LINES)) s_vscroll = (uint8_t)(n - INBOX_VIS_LINES);
  for (i = 0u; i < show; i++) {
    const char *ln = lines[s_vscroll + i];
    fp_text(fp_cx(ln, 12, 0, 127, 1u), (int16_t)(16 + (int16_t)i * 12), ln, 12, 1u, 1u);
  }

  /* 元信息带：两行 */
  fmt_hhmmss(it->rx_ms, buf, sizeof(buf));
  fp_text(4, 40, buf, 12, 1u, 1u);
  if (it->have_rf) snprintf(buf, sizeof(buf), "RSSI %d", (int)it->rssi);
  else snprintf(buf, sizeof(buf), "RSSI --");
  fp_text(76, 40, buf, 12, 1u, 1u);
  fp_text(4, 52, it->path, 12, 1u, 1u);
  fp_text(106, 52, crc_label(it), 12, 1u, 1u);
  lcd_flush();
}

/* 开发自检屏：两档 ASCII 全字符 + 常用汉字（不属于设备 UI） */
static void draw_pattern(void)
{
  char buf[2] = { 0, 0 };
  int cp, i;
  lcd_clear(0);
  for (cp = 0x20, i = 0; cp <= 0x7E; cp++, i++) {
    buf[0] = (char)cp;
    fp_text((int16_t)(2 + (i % 21) * 6), (int16_t)(2 + (i / 21) * 12), buf, 12, 1u, 1u);
  }
  fp_text(2, 50, "本机电量未读高中低周三", 12, 1u, 1u);
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 状态机（design.md §9）                                               */
/* ------------------------------------------------------------------ */
static void redraw(void)
{
  switch (s_view) {
    case UI_SCREEN_IDLE:   draw_idle();   break;
    case UI_SCREEN_UNREAD: draw_unread(); break;
    case UI_SCREEN_INBOX:  draw_inbox();  break;
    default:               draw_idle();   break;
  }
}

/* 未读清零后回到待机；否则停在有未读 */
static void settle_view(void)
{
  s_view = (s_unread > 0u) ? UI_SCREEN_UNREAD : UI_SCREEN_IDLE;
  s_vscroll = 0u;
}

void ui_handle_key(int key)
{
  switch (s_view) {
    case UI_SCREEN_INBOX:
      if (key == SIM_KEY_UP || key == SIM_KEY_DOWN) {
        int dir = (key == SIM_KEY_UP) ? -1 : 1;
        uint8_t n = 0u;
        char lines[WRAP_MAXLINES][WRAP_MAXCELLS * 3 + 1];
        if (s_count > 0u) n = wrap_cells(s_box[s_idx].body, 21u, lines, WRAP_MAXLINES);
        if (n < INBOX_VIS_LINES) n = INBOX_VIS_LINES;
        /* 先滚正文，到底再翻条（design.md §6.3） */
        if (dir > 0 && s_vscroll + INBOX_VIS_LINES < n) { s_vscroll++; }
        else if (dir < 0 && s_vscroll > 0u)             { s_vscroll--; }
        else {
          s_idx = (uint8_t)((s_idx + (dir > 0 ? 1 : (uint8_t)(s_count - 1u))) % s_count);
          s_vscroll = 0u;
        }
        redraw();
      } else if (key == SIM_KEY_OK) {
        if (s_count > 0u) {
          ui_item_t *it = &s_box[s_idx];
          if (!it->read) { it->read = 1u; if (s_unread > 0u) s_unread--; }
          if (s_unread == 0u) { settle_view(); redraw(); return; }
          s_idx = (uint8_t)((s_idx + 1u) % s_count);
          s_vscroll = 0u;
          redraw();
        }
      } else if (key == SIM_KEY_OK_LONG) {
        settle_view();
        redraw();
      }
      break;

    case UI_SCREEN_IDLE:
    case UI_SCREEN_UNREAD:
    default:
      if (key == SIM_KEY_UP || key == SIM_KEY_DOWN) {
        /* 死键：这两态没有可移动的焦点，屏幕不做任何反应（design.md §9） */
      } else if (key == SIM_KEY_OK) {
        /* design.md §9：待机/有未读 ● 短按就进收件箱，空箱也要进（显示"无消息 0 / 0"），
         * 否则用户按下去只有背光会亮、屏幕上没有任何反馈。 */
        s_view = UI_SCREEN_INBOX;
        s_idx = 0u;                 /* 焦点落在第一条（最新在上） */
        s_vscroll = 0u;
        redraw();
      } else if (key == SIM_KEY_OK_LONG) {
        /* 已是最外层：设备上无反应（原型里的提示在演示读数条，不是屏幕内容） */
      }
      break;
  }
}

/* ------------------------------------------------------------------ */
/* 数据注入                                                            */
/* ------------------------------------------------------------------ */
/* 字符串被截断时不许把一个 UTF-8 汉字切成半个。
 * 注意判据是"尾部这一串是否完整"，不是"尾部是不是续字节"：
 * 完整以汉字结尾时，前几版实现会把整个末字误删（位置帧注释末尾丢字就是这个原因）。 */
static void utf8_clip_tail(char *s)
{
  size_t n = strlen(s);
  size_t i = n;
  while (i > 0u && (((uint8_t)s[i - 1u] & 0xC0u) == 0x80u)) i--;   /* 最后一个首字节之后的位置 */
  if (i == 0u) { s[0] = 0; return; }                               /* 整串都是续字节：清空 */
  {
    uint8_t lead = (uint8_t)s[i - 1u];
    size_t need;
    if ((lead & 0x80u) == 0x00u)      need = 1u;
    else if ((lead & 0xE0u) == 0xC0u) need = 2u;
    else if ((lead & 0xF0u) == 0xE0u) need = 3u;
    else if ((lead & 0xF8u) == 0xF0u) need = 4u;
    else { s[i - 1u] = 0; return; }                                /* 非法首字节 */
    if ((n - (i - 1u)) < need) s[i - 1u] = 0;                      /* 这一串不完整才截掉 */
  }
}

static void clip_str(char *dst, uint8_t cap, const char *src)
{
  uint8_t i = 0;
  if (cap == 0u) return;
  while ((uint8_t)(i + 1u) < cap && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
  utf8_clip_tail(dst);
}

/* 重复包（同源同内容，60s 内）：不再丢弃，而是把已入箱的那条刷新时间戳、提到队首、重新标为未读，
 * 这样"重复信息"也会在屏上出现（时间戳是本次接收时刻）。用户 2026-09-18 要求的改动。
 * 返回 1 = 屏上有更新（上层据此点亮背光）；0 = 收件箱里没有对应条目（例如 ackNNN 之类本就不入箱）。 */
static uint8_t ui_bump_duplicate(uint16_t fh, uint32_t t_ms)
{
  uint8_t k;
  for (k = 0u; k < s_count; k++) {
    if (s_box[k].used && s_box[k].hash == fh) break;
  }
  if (k >= s_count) return 0u;

  {
    ui_item_t tmp = s_box[k];
    tmp.rx_ms = t_ms;                    /* 关键：时间戳刷新为本次接收时刻 */
    if (tmp.read) { tmp.read = 0u; s_unread++; }   /* 重新变未读，屏上才会显示 */
    if (k > 0u) {
      memmove(&s_box[1], &s_box[0], sizeof(ui_item_t) * (size_t)k);
      if (s_view == UI_SCREEN_INBOX) {
        if (s_idx == k)     s_idx = 0u;                       /* 光标就在这条上：跟着它到队首 */
        else if (s_idx < k) s_idx = (uint8_t)(s_idx + 1u);     /* 前面插了一条，光标后移 */
      }
    }
    s_box[0] = tmp;                      /* 提到队首（最新在上） */
    s_vscroll = 0u;
    if (s_view == UI_SCREEN_IDLE || s_view == UI_SCREEN_UNREAD) s_view = UI_SCREEN_UNREAD;
    redraw();
    return 1u;
  }
}

uint8_t ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                     uint8_t fixed, uint8_t repeat)
{
  static ax25_decoded_t d;
  static aprs_message_t m;
  static aprs_position_t pos;
  static aprs_mice_t mi;
  char mice_dest[8];
  ui_item_t *it;
  uint8_t i, n;
  uint16_t fh = 0;

  if (!ax25_decode(frame, len, &d)) return 0u;

  {   /* 整帧哈希：去重与"重复包刷新时间戳"都用它 */
    for (uint16_t k = 0; k < len; k++) fh = (uint16_t)((fh << 5) ^ (fh >> 2) ^ frame[k]);
    for (i = 0; i < UI_DUP_N; i++) {
      if (s_dup_src[i][0] && s_dup_hash[i] == fh &&
          strcmp(s_dup_src[i], d.src) == 0 &&
          (uint32_t)(t_ms - s_dup_ms[i]) < 60000u) {
        s_dup_total++;
        s_dup_ms[i] = t_ms;                 /* 去重窗口顺延到本次接收 */
        return ui_bump_duplicate(fh, t_ms); /* 刷新原条目时间戳并提到队首，不再直接丢弃 */
      }
    }
    s_dup_hash[s_dup_pos] = fh;
    clip_str(s_dup_src[s_dup_pos], 7u, d.src);
    s_dup_ms[s_dup_pos] = t_ms;
    s_dup_pos = (uint8_t)((s_dup_pos + 1u) % UI_DUP_N);
  }
  s_rx_total++;

  for (i = 0u; i < 6u; i++) {
    uint8_t c = (uint8_t)((frame[i] >> 1) & 0x7Fu);
    mice_dest[i] = (char)((c == 0u) ? ' ' : (char)c);
  }
  mice_dest[6] = 0;

  /* ackNNN 送达确认：计数后分流（协议流量不进收件箱，design.md §6.4） */
  if (aprs_parse_message(d.info, d.info_len, &m)) {
    char ackbuf[12];
    n = (uint8_t)((m.body_len < (sizeof(ackbuf) - 1u)) ? m.body_len : (sizeof(ackbuf) - 1u));
    for (i = 0u; i < n; i++) ackbuf[i] = (char)m.body[i];
    ackbuf[n] = 0;
    if (is_ack_body(ackbuf)) { s_ack_total++; return 0u; }   /* 按接口约定：ackNNN 只计数，不算入箱 */
  }

  if (s_count >= UI_INBOX_MAX) {           /* 满：丢掉最旧（队尾） */
    if (!s_box[s_count - 1u].read && s_unread > 0u) s_unread--;
    s_count = (uint8_t)(UI_INBOX_MAX - 1u);
  }
  memmove(&s_box[1], &s_box[0], sizeof(ui_item_t) * (size_t)s_count);
  it = &s_box[0];                          /* 新帧插队首：最新在上 */
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->hash = fh;
  it->fixed = fixed;
  it->repeat = repeat;
  it->rx_ms = t_ms;
  it->have_rf = s_have_rf_next;
  it->rssi = s_rssi_next;
  it->snr = s_snr_next;
  s_have_rf_next = 0u;                     /* 采样只消费一次，不跨帧沿用 */
  if (d.src_ssid != 0u)                   /* 带 SSID 时显示成 BG5BLB-12，屏上一格放得下 */
    snprintf(it->src, sizeof(it->src), "%s-%u", d.src, (unsigned)d.src_ssid);
  else
    clip_str(it->src, sizeof(it->src), d.src);
  clip_str(it->dst, sizeof(it->dst), d.dest);

  if (d.npath == 0u) {
    clip_str(it->path, sizeof(it->path), "-");
  } else {
    char tmp[44];
    uint8_t w = 0;
    for (i = 0u; i < d.npath && (uint8_t)(w + 9u) < sizeof(tmp); i++) {
      uint8_t k;
      for (k = 0u; k < 6u && d.path[i][k]; k++) tmp[w++] = (char)d.path[i][k];
      if (d.path_h[i]) tmp[w++] = '*';
      if ((uint8_t)(i + 1u) < d.npath) tmp[w++] = ',';
    }
    tmp[w] = 0;
    clip_str(it->path, sizeof(it->path), tmp);
  }

  if (aprs_parse_mice(mice_dest, d.info, d.info_len, &mi)) {
    it->kind = UI_KIND_MICE;
    it->have_pos = 1u;
    clip_str(it->lat, sizeof(it->lat), mi.lat);
    clip_str(it->lon, sizeof(it->lon), mi.lon);
    /* 注释优先：经纬度在态 2 已有专用格子，正文再抄一遍会把真正的消息文字挤进滚动区。
     * 有注释（消息/备注）就只放注释；纯信标才回退成经纬度 + 类型 + 速度/航向。 */
    if (mi.comment[0] != 0)
      snprintf(it->body, sizeof(it->body), "%.40s", mi.comment);
    else
      snprintf(it->body, sizeof(it->body), "%.9s %.10s %.4s %ukm/h %u",
               mi.lat, mi.lon, mi.mtype,
               (unsigned)mi.speed_kmh, (unsigned)mi.course);
    utf8_clip_tail(it->body);
  } else if (aprs_parse_position(d.info, d.info_len, &pos)) {
    it->kind = UI_KIND_POS;
    it->have_pos = 1u;
    clip_str(it->lat, sizeof(it->lat), pos.lat);
    clip_str(it->lon, sizeof(it->lon), pos.lon);
    if (pos.comment[0] != 0)
      snprintf(it->body, sizeof(it->body), "%.40s", pos.comment);
    else
      snprintf(it->body, sizeof(it->body), "%.9s %.10s", pos.lat, pos.lon);
    utf8_clip_tail(it->body);
  } else if (aprs_parse_message(d.info, d.info_len, &m)) {
    it->kind = UI_KIND_MSG;
    n = (uint8_t)((m.body_len < (UI_BODY_MAX - 1u)) ? m.body_len : (UI_BODY_MAX - 1u));
    memcpy(it->body, m.body, n);
    it->body[n] = 0;
  } else {
    it->kind = UI_KIND_OTHER;
    n = (uint8_t)((d.info_len < (UI_BODY_MAX - 1u)) ? d.info_len : (UI_BODY_MAX - 1u));
    memcpy(it->body, d.info, n);
    it->body[n] = 0;
  }

  s_count++;
  s_unread++;
  /* design.md §9：待机态收到新包要立刻从锁屏切到"有未读"页并重绘；
   * 已经在收件箱里时不抢焦点（但新条插在队首，光标跟着 +1 才是原来那条）。 */
  if (s_view == UI_SCREEN_IDLE || s_view == UI_SCREEN_UNREAD) {
    s_view = UI_SCREEN_UNREAD;
    s_vscroll = 0u;
  } else if (s_view == UI_SCREEN_INBOX) {
    if ((uint16_t)(s_idx + 1u) < s_count) s_idx++;
    s_vscroll = 0u;
  }
  redraw();                      /* 立即刷新，不等 ui_tick 的冒号闪烁 */
  return 1u;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */
void ui_init(void)
{
  lcd_init();
  s_count = 0u; s_idx = 0u; s_vscroll = 0u;
  s_view = UI_SCREEN_IDLE;
  s_rx_total = 0u; s_unread = 0u; s_dup_total = 0u; s_ack_total = 0u;
  s_dup_pos = 0u;
  s_batt = -1;
  s_wday = 0xFFu;
  s_have_rf_next = 0u;
  memset(s_box, 0, sizeof(s_box));
  memset(s_dup_src, 0, sizeof(s_dup_src));
}

void ui_show(int screen)
{
  switch (screen) {
    case UI_SCREEN_PATTERN: draw_pattern(); return;
    case UI_SCREEN_IDLE:
    case UI_SCREEN_UNREAD:
    case UI_SCREEN_INBOX:
      s_view = (uint8_t)screen;
      break;
    default:
      s_view = UI_SCREEN_IDLE;
      break;
  }
  redraw();
}

void ui_set_mycall(const char *call)
{
  if (call && call[0]) clip_str(s_mycall, sizeof(s_mycall), call);
  else clip_str(s_mycall, sizeof(s_mycall), "NOCALL");
}
void ui_set_rx_freq_khz(uint32_t khz) { s_freq_khz = khz; }
void ui_set_clock_ms(uint32_t ms) { s_now_ms = ms; }
void ui_set_wallclock(uint8_t wday, uint8_t mon, uint8_t day)
{ s_wday = (uint8_t)(wday % 7u); s_mon = mon; s_mday = day; }
void ui_set_batt(int8_t level) { s_batt = (level > 2) ? 2 : level; }
void ui_set_radio_stats(int16_t rssi_dbm, int16_t snr)
{
  s_rssi_next = rssi_dbm;
  s_snr_next = snr;
  /* -32768 = 本帧没有采样（I2C 读失败）：清掉标志，界面显示 --，
   * 否则上一帧的读数会被当成这一帧的值（design.md 第 12 节：没有测量就不显示）。 */
  s_have_rf_next = (uint8_t)((rssi_dbm == (int16_t)-32768) ? 0u : 1u);
}

uint16_t ui_inbox_count(void) { return s_count; }
uint16_t ui_unread_count(void) { return s_unread; }
uint16_t ui_rx_total(void) { return s_rx_total; }
uint16_t ui_dup_total(void) { return s_dup_total; }
uint32_t ui_clock_ms(void) { return s_now_ms; }
uint8_t  ui_current_screen(void) { return s_view; }

void ui_tick(uint32_t ms)
{
  static uint8_t colon = 0xFFu;
  s_now_ms += ms;

  /* 只在画面真的会变时重绘：态 1/2 的大格时钟冒号（design.md §4.4） */
  if (s_view == UI_SCREEN_IDLE || s_view == UI_SCREEN_UNREAD) {
    uint8_t c = (uint8_t)((s_now_ms / 500u) & 1u);
    if (c != colon) { colon = c; redraw(); }
  }
}
