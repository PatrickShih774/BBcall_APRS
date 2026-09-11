/*
 * BBcall_APRS UI（复古寻呼机风格）
 *
 * 数据来自真实 AX.25 帧（串口日志回放或 WAV 解调），解析复用固件 ax25.c / aprs.c，
 * 绘图复用固件 lcd_st7567.c（8x16 标题 / 6x8 正文 / 细线 / 反显 / 放大字形）。
 *
 * 统一的 chrome 系统（全屏只用这一套，不在别处套方框）：
 *   y0..7   顶部状态栏：反显，左侧屏幕名，右侧 信号格 + 信封(未读) + 静音
 *   y8      细线
 *   y10..56 内容区，6x8 行网格 y=10/18/26/34/42/50
 *   x124..  列表滚动轨（仅溢出时出现）
 * 选择态一律用「反显条」，这是寻呼机的原生语言。
 */
#include "ui_harness.h"
#include "lcd_st7567.h"
#include "ax25.h"
#include "aprs.h"
#include "msg_store.h"
#include "cn_font.h"
#include <string.h>
#include <stdio.h>

#ifndef UI_FW_VER
#define UI_FW_VER "v0.3"
#endif

/* 行网格 */
#define ROW(i)   ((uint8_t)(10u + (i) * 8u))
#define RAIL_Y   0u
#define RAIL_H   7u
#define SEP_Y    8u
#define FULL_W   124u   /* 有滚动轨时内容宽度上限 */

typedef struct {
  uint8_t  used;
  uint8_t  read;
  uint8_t  kind;
  uint8_t  fixed;
  uint8_t  repeat;
  uint8_t  relay;
  uint8_t  has_time;
  uint16_t seq;
  char     src[10];
  char     dst[10];
  char     path[30];
  char     title[34];
  char     body[UI_BODY_MAX];
  uint32_t rx_ms;
} ui_item_t;

static ui_item_t s_box[UI_INBOX_MAX];
static uint8_t   s_count, s_sel, s_top, s_page, s_confirm;
static uint8_t   s_scr = UI_SCREEN_BOOT;
static uint8_t   s_menu_sel;
static uint8_t   s_msg_hub_sel;       /* Messenger 首页光标 */
static uint8_t   s_msg_sel;           /* Inbox/Sent 光标 */
static uint8_t   s_msg_top;

static uint8_t   s_cn_page;           /* 中文字库样张分页 */
static uint8_t   s_smeter;            /* 0..9 */
static uint8_t   s_muted;
static uint16_t  s_rx_total, s_unread, s_dup_total;
static uint32_t  s_freq_khz = 144640u;
static uint32_t  s_now_ms;
static uint16_t  s_rssi, s_snr, s_afc, s_exn;

#define UI_DUP_N 8
static uint16_t s_dup_hash[UI_DUP_N];
static char     s_dup_src[UI_DUP_N][7];
static uint32_t s_dup_ms[UI_DUP_N];
static uint8_t  s_dup_pos;

/* ------------------------------------------------------------------ */
/* 文本工具                                                            */
/* ------------------------------------------------------------------ */
static void clip(char *dst, uint8_t cap, const char *src)
{
  uint8_t i = 0;
  if (cap == 0u) return;
  while ((uint8_t)(i + 1u) < cap && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

static uint8_t wrap_text(const char *s, char lines[][22], uint8_t maxlines, uint8_t width)
{
  uint8_t n = 0;
  while (*s && n < maxlines) {
    const char *p = s;
    uint8_t len = 0, last_sp = 0;
    while (*p == ' ') p++;
    if (!*p) break;
    while (p[len] && len < width) {
      if (p[len] == ' ') last_sp = len;
      len++;
    }
    if (p[len] && last_sp > 0u) len = last_sp;
    if (len == 0u) break;
    memcpy(lines[n], p, len);
    lines[n][len] = 0;
    s = p + len;
    n++;
  }
  return n;
}

static char kind_char(uint8_t kind)
{
  if (kind == UI_KIND_MSG)  return 'M';
  if (kind == UI_KIND_POS)  return 'P';
  if (kind == UI_KIND_MICE) return 'C';
  return 'X';
}

static const char *kind_tag(uint8_t kind)
{
  if (kind == UI_KIND_MSG)  return "MSG";
  if (kind == UI_KIND_POS)  return "POS";
  if (kind == UI_KIND_MICE) return "MIC-E";
  return "APRS";
}

/* ------------------------------------------------------------------ */
/* 文字绘制                                                            */
/* ------------------------------------------------------------------ */
static void t6(uint8_t x, uint8_t y, const char *s, uint8_t ink)
{ lcd_draw_string6x8(x, y, s, ink); }

static void t6_right(uint8_t y, const char *s, uint8_t ink)
{
  int x = (128 - (int)strlen(s) * 6) / 6 * 6;
  if (x < 0) x = 0;
  if (x > 122) x = 122;
  lcd_draw_string6x8((uint8_t)x, y, s, ink);
}

static void t6_center(uint8_t y, const char *s, uint8_t ink)
{
  int x = ((128 - (int)strlen(s) * 6) / 2) / 6 * 6;
  if (x < 0) x = 0;
  lcd_draw_string6x8((uint8_t)x, y, s, ink);
}

static void hair(uint8_t y) { lcd_hline(0, 127, y, 1); }

/* ------------------------------------------------------------------ */
/* 图标（统一 8x8 网格、1px 线宽、单一家族）                            */
/* ------------------------------------------------------------------ */
static void icon_signal(uint8_t x, uint8_t y, uint8_t bars, uint8_t ink)
{
  static const uint8_t hs[4] = { 3u, 5u, 7u, 8u };
  uint8_t i, k;
  if (bars > 4u) bars = 4u;
  for (i = 0; i < bars; i++) {
    for (k = 0; k < hs[i]; k++)
      lcd_fill_rect((uint8_t)(x + i * 2u), (uint8_t)(y + 7u - k),
                    (uint8_t)(x + i * 2u + 1u), (uint8_t)(y + 7u - k), ink);
  }
}

static void icon_mail(uint8_t x, uint8_t y, uint8_t ink)
{
  lcd_rect(x, (uint8_t)(y + 1u), (uint8_t)(x + 7u), (uint8_t)(y + 6u), ink);
  lcd_pixel((uint8_t)(x + 1u), (uint8_t)(y + 2u), ink);
  lcd_pixel((uint8_t)(x + 6u), (uint8_t)(y + 2u), ink);
  lcd_pixel((uint8_t)(x + 2u), (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 5u), (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 4u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 4u), ink);
}

static void icon_mute(uint8_t x, uint8_t y, uint8_t ink)
{
  uint8_t i;
  lcd_fill_rect((uint8_t)(x + 0u), (uint8_t)(y + 3u), (uint8_t)(x + 1u), (uint8_t)(y + 4u), ink);
  lcd_fill_rect((uint8_t)(x + 2u), (uint8_t)(y + 2u), (uint8_t)(x + 3u), (uint8_t)(y + 5u), ink);
  for (i = 0; i < 8u; i++) lcd_pixel((uint8_t)(x + i), (uint8_t)(y + 7u - i), ink);
}

static void icon_pin(uint8_t x, uint8_t y, uint8_t ink)
{
  lcd_rect((uint8_t)(x + 2u), y, (uint8_t)(x + 5u), (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 4u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 4u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 5u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 5u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 6u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 6u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 7u), ink);
}

static void icon_ant(uint8_t x, uint8_t y, uint8_t ink)
{
  lcd_vline((uint8_t)(x + 3u), (uint8_t)(y + 2u), (uint8_t)(y + 7u), ink);
  lcd_fill_rect((uint8_t)(x + 1u), (uint8_t)(y + 7u), (uint8_t)(x + 5u), (uint8_t)(y + 7u), ink);
  lcd_pixel((uint8_t)(x + 5u), (uint8_t)(y + 1u), ink);
  lcd_pixel((uint8_t)(x + 6u), (uint8_t)(y + 2u), ink);
  lcd_pixel((uint8_t)(x + 6u), (uint8_t)(y + 4u), ink);
  lcd_pixel((uint8_t)(x + 1u), (uint8_t)(y + 1u), ink);
  lcd_pixel((uint8_t)(x + 0u), (uint8_t)(y + 2u), ink);
  lcd_pixel((uint8_t)(x + 0u), (uint8_t)(y + 4u), ink);
}

static void icon_contrast(uint8_t x, uint8_t y, uint8_t ink)
{
  uint8_t r;
  lcd_rect(x, y, (uint8_t)(x + 7u), (uint8_t)(y + 7u), ink);   /* 外框近似圆 */
  for (r = 1; r < 7u; r++) {
    if (r >= 2u && r <= 5u) lcd_vline((uint8_t)(x + 4u), (uint8_t)(y + r), (uint8_t)(y + r), ink);
  }
  lcd_fill_rect((uint8_t)(x + 4u), (uint8_t)(y + 2u), (uint8_t)(x + 5u), (uint8_t)(y + 5u), ink);
}

static void icon_info(uint8_t x, uint8_t y, uint8_t ink)
{
  lcd_fill_rect((uint8_t)(x + 3u), (uint8_t)(y + 1u), (uint8_t)(x + 4u), (uint8_t)(y + 1u), ink);
  lcd_fill_rect((uint8_t)(x + 3u), (uint8_t)(y + 3u), (uint8_t)(x + 4u), (uint8_t)(y + 7u), ink);
  lcd_fill_rect((uint8_t)(x + 2u), (uint8_t)(y + 3u), (uint8_t)(x + 2u), (uint8_t)(y + 3u), ink);
}

static void icon_power(uint8_t x, uint8_t y, uint8_t ink)
{
  lcd_vline((uint8_t)(x + 3u), y, (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 2u), (uint8_t)(y + 1u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 1u), ink);
  lcd_pixel((uint8_t)(x + 1u), (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 5u), (uint8_t)(y + 3u), ink);
  lcd_pixel((uint8_t)(x + 1u), (uint8_t)(y + 5u), ink);
  lcd_pixel((uint8_t)(x + 2u), (uint8_t)(y + 7u), ink);
  lcd_pixel((uint8_t)(x + 3u), (uint8_t)(y + 7u), ink);
  lcd_pixel((uint8_t)(x + 4u), (uint8_t)(y + 7u), ink);
  lcd_pixel((uint8_t)(x + 5u), (uint8_t)(y + 5u), ink);
}
/* 波形：Radio（与其它图标同为 8x8、1px 线宽） */
static void icon_wave(uint8_t x, uint8_t y, uint8_t ink)
{
  static const int8_t wv[8] = { 0, -1, -2, -1, 0, 1, 2, 1 };
  uint8_t i;
  for (i = 0; i < 8u; i++) lcd_pixel((uint8_t)(x + i), (uint8_t)(y + 3 + wv[i]), ink);
}

/* ------------------------------------------------------------------ */
/* Chrome：状态栏 + 滚动轨                                             */
/* ------------------------------------------------------------------ */
static void status_rail(const char *title)
{
  char buf[12];
  lcd_fill_rect(0, RAIL_Y, 127, RAIL_H, 1);      /* 反显底 */
  t6(6, RAIL_Y, title, 0);                       /* 挖字标题（x 对齐 6 像素栅格） */

  /* 右侧：从右往左右对齐排 [信号格] 3px [静音] 3px [未读数] 3px [信封] */
  {
    uint8_t x = 128u;
    uint8_t bars = (uint8_t)((s_smeter + 2u) / 3u);     /* 0..3 档 */
    uint8_t lit = (uint8_t)((bars > 3u) ? 4u : (bars + 1u));
    x = (uint8_t)(x - 8u);
    icon_signal(x, RAIL_Y, lit, 0);
    x = (uint8_t)(x - 3u);
    if (s_muted) {
      x = (uint8_t)(x - 8u);
      icon_mute(x, RAIL_Y, 0);
      x = (uint8_t)(x - 3u);
    }
    if (s_unread > 0u) {
      snprintf(buf, sizeof(buf), "%u", (unsigned)(s_unread > 99u ? 99u : s_unread));
      x = (uint8_t)(x - (uint8_t)(strlen(buf) * 6u));
      t6(x, RAIL_Y, buf, 0);
      x = (uint8_t)(x - 3u);
    }
    x = (uint8_t)(x - 8u);
    icon_mail(x, RAIL_Y, 0);
  }
}

static void scroll_rail(uint8_t top, uint8_t count, uint8_t vis)
{
  uint8_t ty = ROW(0), th, hh, hy;
  if (count <= vis) return;
  th = (uint8_t)(vis * 8u - 2u);
  hh = (uint8_t)((uint16_t)th * vis / count);
  if (hh < 3u) hh = 3u;
  hy = (uint8_t)(ty + (uint16_t)(th - hh) * top / (count - vis));
  lcd_fill_rect(126, hy, 127, (uint8_t)(hy + hh - 1u), 1);
}

/* ------------------------------------------------------------------ */
/* 主屏                                                                */
/* ------------------------------------------------------------------ */
static void draw_boot(void)
{
  char buf[24];
  lcd_clear(0);
  lcd_draw_string8x16_scaled(16, 6, "BBCALL", 1, 2);
  t6_center(44, "APRS RX PAGER", 1);
  snprintf(buf, sizeof(buf), "FW " UI_FW_VER);
  t6_center(54, buf, 1);
  lcd_flush();
}

static void clock_hhmm(char *out, uint8_t cap)
{
  uint32_t sec = s_now_ms / 1000u;
  uint32_t hh = (sec / 3600u) % 24u;
  uint32_t mm = (sec / 60u) % 60u;
  snprintf(out, cap, "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
  (void)sec;
}

static void draw_home(void)
{
  char buf[40];
  char clk[8];
  lcd_clear(0);
  status_rail("STATUS");
  hair(SEP_Y);

  clock_hhmm(clk, sizeof(clk));
  /* 大时钟 16x32；冒号按 0.5s 闪烁（唯一的动效：设备存活反馈） */
  if (!((s_now_ms / 500u) & 1u)) clk[2] = ' ';
  lcd_draw_string8x16_scaled(24, 11, clk, 1, 2);
  t6(6, 23, "UP", 1);                 /* 明示这是开机计时，不是墙上时钟 */

  hair(44);

  snprintf(buf, sizeof(buf), "%lu.%03lu MHz",
           (unsigned long)(s_freq_khz / 1000u), (unsigned long)(s_freq_khz % 1000u));
  t6(0, 46, buf, 1);
  snprintf(buf, sizeof(buf), "RX %u", (unsigned)s_rx_total);
  t6_right(46, buf, 1);

  if (s_count > 0u) {
    const ui_item_t *it = &s_box[0];   /* 队首 = 最新 */
    snprintf(buf, sizeof(buf), "%-6.6s %s", it->src, kind_tag(it->kind));
    t6(0, 54, buf, 1);
    snprintf(buf, sizeof(buf), "%.7s", it->title);   /* 只放得下 7 字符，与左侧留 6px 间隙 */
    t6_right(54, buf, 1);
  } else {
    t6(0, 54, "no message", 1);
    t6_right(54, "waiting...", 1);
  }
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 菜单                                                                */
/* ------------------------------------------------------------------ */
#define MENU_N 6
/* 二级菜单：只放设置与诊断。主功能（消息）是根屏，不在这里。 */
static const char *const s_menu_label[MENU_N] = {
  "Status", "Heard", "Radio", "Contrast", "Backlight", "About"
};

static void menu_icon(uint8_t i, uint8_t x, uint8_t y, uint8_t ink)
{
  switch (i) {
    case 0: icon_mail(x, y, ink);     break;
    case 1: icon_ant(x, y, ink);      break;
    case 2: icon_wave(x, y, ink);     break;
    case 3: icon_contrast(x, y, ink); break;
    case 4: icon_power(x, y, ink);    break;
    default: icon_info(x, y, ink);    break;
  }
}

static void draw_menu(void)
{
  char buf[24];
  uint8_t i;
  lcd_clear(0);
  status_rail("MENU");
  hair(SEP_Y);

  for (i = 0; i < MENU_N; i++) {
    uint8_t y = ROW(i);
    uint8_t sel = (i == s_menu_sel) ? 1u : 0u;
    uint8_t ink = sel ? 0u : 1u;
    if (sel) lcd_fill_rect(0, y, 123, (uint8_t)(y + 7u), 1);
    menu_icon(i, 2, y, ink);
    t6(12, y, s_menu_label[i], ink);
    if (i == 0 && s_unread > 0u) {
      snprintf(buf, sizeof(buf), "%u", (unsigned)s_unread);
      t6((uint8_t)(((120 - (int)strlen(buf) * 6) / 6) * 6), y, buf, ink);
    }
  }
  scroll_rail(0, MENU_N, MENU_N);
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 收件箱 / 详情                                                       */
/* ------------------------------------------------------------------ */
static void draw_inbox(void)
{
  char line[26];
  char hdr[24];
  uint8_t n = s_count, i, vis = 6u;
  lcd_clear(0);
  snprintf(hdr, sizeof(hdr), "HEARD %u/%u",
           (unsigned)(n ? s_sel + 1u : 0u), (unsigned)n);
  status_rail(hdr);
  hair(SEP_Y);

  if (n == 0u) {
    t6(0, ROW(1), "no station yet", 1);
    t6(0, ROW(4), "M=menu  S=home", 1);
    lcd_flush();
    return;
  }
  if (s_sel >= n) s_sel = (uint8_t)(n - 1u);
  if (s_sel < s_top) s_top = s_sel;
  if ((uint16_t)(s_top + vis - 1u) < s_sel) s_top = (uint8_t)(s_sel - (vis - 1u));
  if ((uint16_t)s_top + vis > n) s_top = (n > vis) ? (uint8_t)(n - vis) : 0u;

  for (i = 0; i < vis; i++) {
    uint8_t nth = (uint8_t)(s_top + i);
    const ui_item_t *it;
    uint8_t sel;
    if (nth >= n) break;
    it = &s_box[nth];
    sel = (nth == s_sel) ? 1u : 0u;
    snprintf(line, sizeof(line), "%-6.6s %c %-10.10s%c",
             it->src, kind_char(it->kind), it->title, it->read ? ' ' : '*');
    if (sel) {
      lcd_fill_rect(0, ROW(i), 123, (uint8_t)(ROW(i) + 7u), 1);
      t6(0, ROW(i), line, 0);
    } else {
      t6(0, ROW(i), line, 1);
    }
  }
  scroll_rail(s_top, n, vis);
  lcd_flush();
}

static void draw_detail(void)
{
  char lines[14][22];
  char hdr[24], foot[64];
  const ui_item_t *it;
  uint8_t n, i, pages;

  if (s_count == 0u) { draw_inbox(); return; }
  if (s_sel >= s_count) s_sel = (uint8_t)(s_count - 1u);
  it = &s_box[s_sel];

  n = wrap_text(it->body, lines, 14u, 20u);
  if (n == 0u) { lines[0][0] = 0; n = 1u; }
  pages = (uint8_t)((n + 4u) / 5u);
  if (pages == 0u) pages = 1u;
  if (s_page >= pages) s_page = (uint8_t)(pages - 1u);

  lcd_clear(0);
  snprintf(hdr, sizeof(hdr), "%s %s", kind_tag(it->kind), it->src);
  status_rail(hdr);
  hair(SEP_Y);

  for (i = 0; i < 5u; i++) {
    uint8_t idx = (uint8_t)(s_page * 5u + i);
    if (idx < n) t6(0, ROW(i), lines[idx], 1);
  }

  snprintf(foot, sizeof(foot), "%u/%u", (unsigned)(s_page + 1u), (unsigned)pages);
  if (it->relay)       { strncat(foot, " RELAY ", sizeof(foot) - strlen(foot) - 1u);
                         strncat(foot, it->path, sizeof(foot) - strlen(foot) - 1u); }
  else if (it->fixed)  strncat(foot, " FIX", sizeof(foot) - strlen(foot) - 1u);
  else if (it->repeat) strncat(foot, " REP", sizeof(foot) - strlen(foot) - 1u);
  t6(0, ROW(5), foot, 1);
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 电台状态 / 关于                                                     */
/* ------------------------------------------------------------------ */
static void draw_radio(void)
{
  char buf[28];
  lcd_clear(0);
  status_rail("RADIO");
  hair(SEP_Y);

  snprintf(buf, sizeof(buf), "%lu.%03lu MHz  S%u",
           (unsigned long)(s_freq_khz / 1000u), (unsigned long)(s_freq_khz % 1000u),
           (unsigned)s_smeter);
  t6(0, ROW(0), buf, 1);

  snprintf(buf, sizeof(buf), "RSSI %-5u SNR %-5u", (unsigned)s_rssi, (unsigned)s_snr);
  t6(0, ROW(1), buf, 1);

  snprintf(buf, sizeof(buf), "AFC  %-5u EXN %-5u", (unsigned)s_afc, (unsigned)s_exn);
  t6(0, ROW(2), buf, 1);

  snprintf(buf, sizeof(buf), "RX %u  DUP %u", (unsigned)s_rx_total, (unsigned)s_dup_total);
  t6(0, ROW(3), buf, 1);

  t6(0, ROW(4), s_muted ? "audio: muted" : "audio: on", 1);
  t6(0, ROW(5), "BACK=MENU", 1);
  lcd_flush();
}

static void draw_about(void)
{
  char buf[24];
  lcd_clear(0);
  status_rail("ABOUT");
  hair(SEP_Y);
  t6(0, ROW(0), "BBCALL_APRS", 1);
  t6(0, ROW(1), "FW " UI_FW_VER "  GPL-3.0", 1);
  t6(0, ROW(2), "STM32F103C8T6", 1);
  t6(0, ROW(3), "ST7567 128x64 LCD", 1);
  t6(0, ROW(4), "BK4802P 21.25MHz IF", 1);
  {                                   /* 中文字库状态如实显示，便于真机核对 */
    uint16_t cnc = cn_font_count();
    if (cnc > 0u) snprintf(buf, sizeof(buf), "CN FONT %u", (unsigned)cnc);
    else          snprintf(buf, sizeof(buf), "CN FONT OFF");
    t6(0, ROW(5), buf, 1);
  }
  lcd_flush();
}

static void draw_pattern(void)
{
  int x, y;
  lcd_clear(0);
  for (y = 0; y < LCD_H; y++)
    for (x = 0; x < LCD_W; x++)
      if (((x / 4) + (y / 4)) & 1) lcd_pixel((uint8_t)x, (uint8_t)y, 1);
  lcd_line(0, 0, 127, 63, 1);
  lcd_line(127, 0, 0, 63, 1);
  lcd_draw_string8x16(16, 8, "ST7567 SIM", 0);
  lcd_draw_string6x8(5, 28, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0);
  lcd_draw_string6x8(5, 38, "abcdefghijklmnopqrstuvwxyz", 0);
  lcd_draw_string6x8(5, 48, "0123456789 !#$%&*()+-./:;<=>?", 0);
  lcd_flush();
}

/* 删除确认：内嵌双线框的模态（寻呼机原生做法） */
static void draw_confirm(void)
{
  lcd_fill_rect(6, 18, 121, 45, 1);
  lcd_rect(8, 20, 119, 43, 0);
  t6_center(26, "DELETE MESSAGE?", 0);
  t6_center(34, "OK=YES  BACK=NO", 0);
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Messenger：只保留「收件箱 + 阅读」                                    */
/* 本项目仅接收，COMPOSE/SENT 是死路（前者只能存草稿，后者永远为空），   */
/* 且 HEARD 已在主菜单里，所以不再单独做一层 Messenger 首页。            */
/* 版面规范见 UISkill.md 第 11 节；点状分隔线借自 GOGUFW。               */
/* ------------------------------------------------------------------ */
static void dotted_sep(uint8_t y)
{
  uint8_t x;
  for (x = 0; x < 128u; x = (uint8_t)(x + 4u)) lcd_hline(x, (uint8_t)(x + 1u), y, 1);
}

static void draw_msg_list(void)
{
  char line[26];
  char hdr[24];
  char age[6];
  uint8_t n = msg_store_count_inbox();
  uint8_t i, vis = 6u;

  lcd_clear(0);
  snprintf(hdr, sizeof(hdr), "INBOX %u/%u",
           (unsigned)(n ? s_msg_sel + 1u : 0u), (unsigned)n);
  status_rail(hdr);
  hair(SEP_Y);

  if (n == 0u) {
    t6(0, ROW(2), "no message yet", 1);
    t6(0, ROW(4), "BACK=MENU", 1);
    lcd_flush();
    return;
  }
  if (s_msg_sel >= n) s_msg_sel = (uint8_t)(n - 1u);
  if (s_msg_sel < s_msg_top) s_msg_top = s_msg_sel;
  if ((uint16_t)(s_msg_top + vis - 1u) < s_msg_sel) s_msg_top = (uint8_t)(s_msg_sel - (vis - 1u));
  if ((uint16_t)s_msg_top + vis > n) s_msg_top = (n > vis) ? (uint8_t)(n - vis) : 0u;

  for (i = 0; i < vis; i++) {
    uint8_t idx = (uint8_t)(s_msg_top + i);
    msg_in_t *m;
    if (idx >= n) break;
    m = msg_store_inbox(idx);
    msg_store_fmt_age(m->age_s, age, sizeof(age));
    snprintf(line, sizeof(line), "%c%-14.14s%5s", m->unread ? '*' : ' ', m->text, age);
    if (idx == s_msg_sel) {
      lcd_fill_rect(0, ROW(i), 123, (uint8_t)(ROW(i) + 7u), 1);
      t6(0, ROW(i), line, 0);
    } else {
      t6(0, ROW(i), line, 1);
    }
  }
  scroll_rail(s_msg_top, n, vis);
  lcd_flush();
}

static void draw_msg_read(void)
{
  char lines[8][22];
  char buf[32];
  char age[6];
  const msg_in_t *m;
  uint8_t n, i;

  m = msg_store_inbox(s_msg_sel);
  if (!m || !m->used) { draw_msg_list(); return; }
  msg_store_fmt_age(m->age_s, age, sizeof(age));

  n = wrap_text(m->text, lines, 4u, 20u);
  if (n == 0u) { lines[0][0] = 0; n = 1u; }

  lcd_clear(0);
  snprintf(buf, sizeof(buf), "READ %u/%u",
           (unsigned)(s_msg_sel + 1u), (unsigned)msg_store_count_inbox());
  status_rail(buf);
  hair(SEP_Y);

  snprintf(buf, sizeof(buf), "FROM:%s", m->from);
  t6(0, ROW(0), buf, 1);
  t6_right(ROW(0), age, 1);

  dotted_sep(ROW(1));
  for (i = 0; i < 4u; i++) {
    if (i < n) t6(0, (uint8_t)(ROW(1) + 2u + i * 8u), lines[i], 1);
  }
  dotted_sep(52);

  t6(0, 54, "BACK", 1);
  t6_right(54, "DEL", 1);
  lcd_flush();
}
/* ------------------------------------------------------------------ */
/* 中文字库样张：8 列 x 3 行 = 24 字/页，直接在屏上验字库与索引          */
/* ------------------------------------------------------------------ */
static void draw_cnfont(void)
{
  char hdr[24];
  uint16_t total = cn_font_count();
  uint8_t per = 24u;
  uint8_t pages, i;

  lcd_clear(0);
  if (total == 0u) {                       /* CN_FONT_ENABLED=0 时如实说明 */
    status_rail("CN FONT");
    hair(SEP_Y);
    t6(0, ROW(2), "CN font disabled", 1);
    t6(0, ROW(4), "run tools/gen_cn_font.py", 1);
    lcd_flush();
    return;
  }
  pages = (uint8_t)((total + per - 1u) / per);
  if (pages == 0u) pages = 1u;
  if (s_cn_page >= pages) s_cn_page = (uint8_t)(pages - 1u);

  snprintf(hdr, sizeof(hdr), "CN %u/%u", (unsigned)(s_cn_page + 1u), (unsigned)pages);
  status_rail(hdr);
  hair(SEP_Y);

  for (i = 0; i < per; i++) {
    uint16_t idx = (uint16_t)(s_cn_page * per + i);
    if (idx >= total) break;
    lcd_draw_cn16((uint8_t)((i % 8u) * 16u),
                  (uint8_t)(10u + (i / 8u) * 16u),
                  cn_font_code_at(idx), 1);
  }
  lcd_flush();
}
static void redraw(void)
{
  switch (s_scr) {
    case UI_SCREEN_HOME:   draw_home();   break;
    case UI_SCREEN_MENU:   draw_menu();   break;
    case UI_SCREEN_INBOX:  draw_inbox();  break;
    case UI_SCREEN_DETAIL: draw_detail(); break;
    case UI_SCREEN_RADIO:  draw_radio();  break;
    case UI_SCREEN_ABOUT:  draw_about();  break;
    case UI_SCREEN_BOOT:   draw_boot();   break;
    case UI_SCREEN_MSG_INBOX:   draw_msg_list();    break;
    case UI_SCREEN_MSG_READ:    draw_msg_read();    break;
    case UI_SCREEN_CNFONT:      draw_cnfont();      break;
    default:               draw_pattern();break;
  }
  if (s_confirm) draw_confirm();
}

/* ------------------------------------------------------------------ */
/* 数据注入                                                            */
/* ------------------------------------------------------------------ */
static void delete_sel(void)
{
  uint8_t idx;
  if (s_count == 0u) return;
  idx = s_sel;
  if (!s_box[idx].read) {
    s_box[idx].read = 1u;
    if (s_unread > 0u) s_unread--;
  }
  if ((uint16_t)(idx + 1u) < s_count)
    memmove(&s_box[idx], &s_box[idx + 1u], sizeof(ui_item_t) * (size_t)(s_count - idx - 1u));
  s_count--;
  if (s_sel >= s_count) s_sel = s_count ? (uint8_t)(s_count - 1u) : 0u;
  s_top = 0u;
}

uint8_t ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                     uint8_t fixed, uint8_t repeat)
{
  static ax25_decoded_t d;
  static aprs_message_t m;
  static aprs_position_t pos;
  static aprs_mice_t mi;
  char mice_dest[8];
  char text[UI_BODY_MAX];
  ui_item_t *it;
  uint8_t i, n;

  if (!ax25_decode(frame, len, &d)) return 0u;

  {
    uint16_t fh = 0, k;
    for (k = 0; k < len; k++) fh = (uint16_t)((fh << 5) ^ (fh >> 2) ^ frame[k]);
    for (i = 0; i < UI_DUP_N; i++) {
      if (s_dup_src[i][0] && s_dup_hash[i] == fh &&
          strcmp(s_dup_src[i], d.src) == 0 &&
          (uint32_t)(t_ms - s_dup_ms[i]) < 60000u) {
        s_dup_total++;
        return 0u;
      }
    }
    s_dup_hash[s_dup_pos] = fh;
    clip(s_dup_src[s_dup_pos], 7u, d.src);
    s_dup_ms[s_dup_pos] = t_ms;
    s_dup_pos = (uint8_t)((s_dup_pos + 1u) % UI_DUP_N);
  }
  s_rx_total++;

  for (i = 0; i < 6u; i++) {
    uint8_t c = (uint8_t)((frame[i] >> 1) & 0x7Fu);
    mice_dest[i] = (char)((c == 0u) ? ' ' : (char)c);
  }
  mice_dest[6] = 0;

  if (s_count >= UI_INBOX_MAX) {           /* 满：丢掉最旧（队尾） */
    if (!s_box[s_count - 1u].read && s_unread > 0u) s_unread--;
    s_count = (uint8_t)(UI_INBOX_MAX - 1u);
  }
  memmove(&s_box[1], &s_box[0], sizeof(ui_item_t) * (size_t)s_count);
  it = &s_box[0];                          /* 新帧插到队首：列表最新在上 */
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->fixed = fixed;
  it->repeat = repeat;
  it->rx_ms = t_ms;
  it->has_time = (t_ms > 0u) ? 1u : 0u;
  clip(it->src, sizeof(it->src), d.src);
  clip(it->dst, sizeof(it->dst), d.dest);

  if (d.npath == 0u) {
    clip(it->path, sizeof(it->path), "-");
  } else {
    char tmp[44];
    uint8_t w = 0;
    it->relay = 1u;
    for (i = 0; i < d.npath && (uint8_t)(w + 9u) < sizeof(tmp); i++) {
      uint8_t k;
      for (k = 0; k < 6u && d.path[i][k]; k++) tmp[w++] = (char)d.path[i][k];
      if (d.path_h[i]) tmp[w++] = '*';
      if ((uint8_t)(i + 1u) < d.npath) tmp[w++] = ',';
    }
    tmp[w] = 0;
    clip(it->path, sizeof(it->path), tmp);
  }

  if (aprs_parse_mice(mice_dest, d.info, d.info_len, &mi)) {
    it->kind = UI_KIND_MICE;
    snprintf(it->title, sizeof(it->title), "%s %s", mi.lat, mi.lon);
    snprintf(it->body, sizeof(it->body), "%s %s %s %ukm/h %u %s",
             mi.lat, mi.lon, mi.mtype,
             (unsigned)mi.speed_kmh, (unsigned)mi.course, mi.comment);
  } else if (aprs_parse_position(d.info, d.info_len, &pos)) {
    it->kind = UI_KIND_POS;
    snprintf(it->title, sizeof(it->title), "%s %s", pos.lat, pos.lon);
    snprintf(it->body, sizeof(it->body), "%s %s %s", pos.lat, pos.lon, pos.comment);
  } else if (aprs_parse_message(d.info, d.info_len, &m)) {
    it->kind = UI_KIND_MSG;
    n = (uint8_t)((m.body_len < (UI_BODY_MAX - 1u)) ? m.body_len : (UI_BODY_MAX - 1u));
    for (i = 0; i < n; i++) {
      uint8_t c = m.body[i];
      text[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
    }
    text[n] = 0;
    clip(it->body, sizeof(it->body), text);
    clip(it->title, sizeof(it->title), text);
    {
      /* 同步进 Messenger 收件箱；正文形如 ackNNN 时由 store 分流成送达确认 */
      uint16_t mid = 0u;
      if (m.has_msg_id) {
        uint8_t k;
        for (k = 0; k < 8u && m.msg_id[k] >= '0' && m.msg_id[k] <= '9'; k++)
          mid = (uint16_t)(mid * 10u + (uint16_t)(m.msg_id[k] - '0'));
      }
      msg_store_add_inbox_id(d.src, text, mid);
    }
  } else {
    it->kind = UI_KIND_OTHER;
    n = (uint8_t)((d.info_len < (UI_BODY_MAX - 1u)) ? d.info_len : (UI_BODY_MAX - 1u));
    for (i = 0; i < n; i++) {
      uint8_t c = d.info[i];
      text[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
    }
    text[n] = 0;
    clip(it->body, sizeof(it->body), text);
    clip(it->title, sizeof(it->title), text);
  }

  s_count++;
  s_unread++;
  s_sel = 0u;
  s_top = 0u;
  return 1u;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */
void ui_init(void)
{
  lcd_init();
  s_count = 0u; s_sel = 0u; s_top = 0u; s_page = 0u; s_confirm = 0u;
  s_menu_sel = 0u; s_msg_sel = 0u;
  s_msg_top = 0u; s_cn_page = 0u;
  msg_store_init();
  s_rx_total = 0u; s_unread = 0u; s_dup_total = 0u; s_dup_pos = 0u;
  s_smeter = 0u; s_muted = 1u;
  s_rssi = 0u; s_snr = 0u; s_afc = 0u; s_exn = 0u;
  memset(s_box, 0, sizeof(s_box));
  memset(s_dup_src, 0, sizeof(s_dup_src));
  s_scr = UI_SCREEN_BOOT;
  draw_boot();
}

void ui_show(int screen)
{
  if (screen == UI_SCREEN_CONFIRM) {
    s_scr = UI_SCREEN_INBOX;
    s_confirm = 1u;
    redraw();
    return;
  }
  s_scr = (uint8_t)screen;
  s_page = 0u;
  s_confirm = 0u;
  redraw();
}

void ui_handle_key(int key)
{
  if (s_confirm) {
    if (key == 3) {
      s_confirm = 0u;
      delete_sel();
      s_scr = UI_SCREEN_INBOX;
      redraw();
    } else if (key == 4 || key == 8 || key == 7) {
      s_confirm = 0u;
      redraw();
    }
    return;
  }

  switch (key) {
    case 5:  ui_show(UI_SCREEN_PATTERN); return;   /* T 图案 */
    case 9:  ui_show(UI_SCREEN_MENU);    return;   /* M 菜单 */
    case 7:  ui_show(UI_SCREEN_HOME);    return;   /* S 主页 */
  }

  switch (s_scr) {
    case UI_SCREEN_HOME:
      if (key == 1 || key == 2 || key == 3 || key == 4) ui_show(UI_SCREEN_MENU);
      break;

    case UI_SCREEN_MENU:
      if (key == 1) { if (s_menu_sel > 0u) s_menu_sel--; draw_menu(); }
      else if (key == 2) { if (s_menu_sel + 1u < MENU_N) s_menu_sel++; draw_menu(); }
      else if (key == 3) {
        switch (s_menu_sel) {
          case 0: ui_show(UI_SCREEN_HOME); break;
          case 1: s_sel = 0u; s_top = 0u; ui_show(UI_SCREEN_INBOX); break;
          case 2: ui_show(UI_SCREEN_RADIO); break;
          case 3: break;                       /* Contrast：真机改 0x81 值 */
          case 4: lcd_backlight(0); break;     /* Backlight：真机 PB0 */
          case 5: ui_show(UI_SCREEN_ABOUT); break;
          default: break;
        }
      }
      else if (key == 4) ui_show(UI_SCREEN_MSG_INBOX);   /* 根屏是消息列表 */
      break;

    case UI_SCREEN_INBOX:
      if (key == 1) { if (s_sel > 0u) s_sel--; draw_inbox(); }
      else if (key == 2) { if (s_sel + 1u < s_count) s_sel++; draw_inbox(); }
      else if (key == 3) {
        if (s_count > 0u) {
          uint8_t idx = s_sel;
          if (!s_box[idx].read) { s_box[idx].read = 1u; if (s_unread > 0u) s_unread--; }
          s_page = 0u;
          ui_show(UI_SCREEN_DETAIL);
        }
      }
      else if (key == 4) ui_show(UI_SCREEN_MENU);
      else if (key == 8 && s_count > 0u) { s_confirm = 1u; redraw(); }
      break;

    case UI_SCREEN_DETAIL:
      if (key == 1) { if (s_page > 0u) s_page--; else if (s_sel > 0u) s_sel--; draw_detail(); }
      else if (key == 2) { s_page++; draw_detail(); }
      else if (key == 4) ui_show(UI_SCREEN_INBOX);
      else if (key == 8) { s_confirm = 1u; redraw(); }
      break;

    case UI_SCREEN_RADIO:
    case UI_SCREEN_ABOUT:
      if (key == 4 || key == 3) ui_show(UI_SCREEN_MENU);
      break;

    case UI_SCREEN_MSG_INBOX:
      if (key == 1) { if (s_msg_sel > 0u) s_msg_sel--; draw_msg_list(); }
      else if (key == 2) {
        if (s_msg_sel + 1u < msg_store_count_inbox()) s_msg_sel++;
        draw_msg_list();
      }
      else if (key == 3) {
        if (msg_store_count_inbox() > 0u) {
          msg_store_mark_read(s_msg_sel);
          ui_show(UI_SCREEN_MSG_READ);
        }
      }
      else if (key == 4) ui_show(UI_SCREEN_MENU);
      break;

    case UI_SCREEN_MSG_READ:
      if (key == 1) { if (s_msg_sel > 0u) s_msg_sel--; draw_msg_read(); }
      else if (key == 2) {
        if (s_msg_sel + 1u < msg_store_count_inbox()) s_msg_sel++;
        draw_msg_read();
      }
      else if (key == 3 || key == 4) ui_show(UI_SCREEN_MSG_INBOX);
      break;
    case UI_SCREEN_CNFONT:
      if (key == 1) { if (s_cn_page > 0u) s_cn_page--; draw_cnfont(); }
      else if (key == 2) { s_cn_page++; draw_cnfont(); }
      else if (key == 4 || key == 3) ui_show(UI_SCREEN_MENU);
      break;

    default:
      if (key == 4) ui_show(UI_SCREEN_MSG_INBOX);   /* 根屏是消息列表 */
      break;
  }
}

void ui_set_rx_freq_khz(uint32_t khz) { s_freq_khz = khz; }
void ui_set_smeter(uint8_t s) { s_smeter = (s > 9u) ? 9u : s; }
void ui_set_muted(uint8_t muted) { s_muted = muted ? 1u : 0u; }
void ui_set_clock_ms(uint32_t ms) { s_now_ms = ms; }
void ui_set_radio_stats(uint16_t rssi, uint16_t snr, uint16_t afc, uint16_t exn)
{ s_rssi = rssi; s_snr = snr; s_afc = afc; s_exn = exn; }

uint16_t ui_inbox_count(void) { return s_count; }
uint16_t ui_unread_count(void) { return s_unread; }
uint16_t ui_rx_total(void) { return s_rx_total; }
uint16_t ui_dup_total(void) { return s_dup_total; }

void ui_tick(uint32_t ms)
{
  static uint8_t  colon = 0xFFu;
  static uint16_t last_s = 0xFFFFu;
  s_now_ms += ms;
  msg_store_tick(ms);

  /* 只在画面内容真的会变时才重绘（UISkill 2.2） */
  if (s_scr == UI_SCREEN_HOME) {
    uint8_t c = (uint8_t)((s_now_ms / 500u) & 1u);   /* 冒号闪烁 */
    if (c != colon) { colon = c; draw_home(); }
    return;
  }
  if (s_scr == UI_SCREEN_MSG_INBOX) {
    uint16_t s = (uint16_t)(s_now_ms / 1000u);       /* 年龄列按秒变 */
    if (s != last_s) { last_s = s; draw_msg_list(); }
  }
}
