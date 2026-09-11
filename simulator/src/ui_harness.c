/*
 * 模拟器 UI harness
 *
 * 复刻 BB 机的待机 / 收件箱 / 详情 / 删除交互；数据来自真实 AX.25 帧
 * （串口日志回放或 WAV 解调），解析复用固件 ax25.c / aprs.c，绘图复用 lcd_st7567.c。
 *
 * 版面：8x16 大字号用于标题（16 字符/行），6x8 小字号用于列表与正文（21 字符/行）。
 *       实测 6x8 每屏可用 6 行（y=16/24/32/40/48/56），配合 16px 标题 + 分隔线。
 *
 * 屏幕：开机 / 待机 / 收件箱 / 详情 / 测试图案
 * 按键：↑↓ 选择、Enter 打开、Backspace 返回、Delete 删除（二次确认）、
 *       T 图案、M 收件箱、S 待机
 */
#include "ui_harness.h"
#include "lcd_st7567.h"
#include "ax25.h"
#include "aprs.h"
#include <string.h>
#include <stdio.h>

#ifndef UI_FW_VER
#define UI_FW_VER "v0.3"
#endif

#define ROW6(i) ((uint8_t)(16u + (i) * 8u))

typedef struct {
  uint8_t  used;
  uint8_t  read;        /* 0=未读，1=已读 */
  uint8_t  kind;        /* UI_KIND_* */
  uint8_t  fixed;       /* [FIX]/[FIX2] 纠错恢复 */
  uint8_t  repeat;      /* [REP] 参考帧恢复 */
  uint8_t  relay;       /* 含中继路径 */
  uint8_t  has_time;    /* 日志/回放提供了时间戳 */
  uint16_t seq;         /* 本次会话内的接收序号 */
  char     src[10];
  char     dst[10];
  char     path[30];
  char     title[34];   /* 单行摘要：消息正文或"纬度 经度" */
  char     body[UI_BODY_MAX];
  uint32_t rx_ms;
} ui_item_t;

static ui_item_t s_box[UI_INBOX_MAX];
static uint8_t   s_count;
static uint8_t   s_sel;
static uint8_t   s_top;
static uint8_t   s_scr = UI_SCREEN_PATTERN;
static uint8_t   s_page;
static uint8_t   s_confirm;      /* 删除二次确认 */
static uint16_t  s_rx_total;
static uint16_t  s_unread;
static uint32_t  s_freq_khz = 144640u;
static uint32_t  s_now_ms;

/* 重复包抑制（与固件 bbcall_app.c 一致）：同一帧 60s 内只入箱一次。
 * 16 路并行相位走廊会用同一帧各解一遍，必须抑制。 */
#define UI_DUP_N 8
static uint16_t s_dup_hash[UI_DUP_N];
static char     s_dup_src[UI_DUP_N][7];
static uint32_t s_dup_ms[UI_DUP_N];
static uint8_t  s_dup_pos;
static uint16_t s_dup_total;

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

/* 折行：优先在空格断开，单词过长则硬折 */
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
/* 绘制小工具                                                          */
/* ------------------------------------------------------------------ */
static void text8(uint8_t y, const char *s) { lcd_draw_string8x16(0, y, s, 1); }

/* 居中/右对齐都吸附到字符栅格（8 或 6 的整数倍），否则字会落在半个字符上 */
static void text8_center(uint8_t y, const char *s)
{
  int x = ((128 - (int)strlen(s) * 8) / 2) & ~7;
  if (x < 0) x = 0;
  lcd_draw_string8x16((uint8_t)x, y, s, 1);
}

static void text6(uint8_t y, const char *s) { lcd_draw_string6x8(0, y, s, 1); }

static void text6_right(uint8_t y, const char *s)
{
  int x = (128 - (int)strlen(s) * 6) / 6 * 6;
  if (x < 0) x = 0;
  if (x > 122) x = 122;
  lcd_draw_string6x8((uint8_t)x, y, s, 1);
}

/* sel=1 时反显（填充 + 挖字），用于列表选中行 */
static void row6(uint8_t y, const char *s, uint8_t sel)
{
  if (sel) {
    lcd_fill_rect(0, y, 127, (uint8_t)(y + 7u), 1);
    lcd_draw_string6x8(0, y, s, 0);
  } else {
    lcd_draw_string6x8(0, y, s, 1);
  }
}

static void sep(uint8_t y) { lcd_line(0, y, 127, y, 1); }

/* ------------------------------------------------------------------ */
/* 画面                                                                */
/* ------------------------------------------------------------------ */
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

static void draw_boot(void)
{
  lcd_clear(0);
  text8_center(0, "BBCALL APRS");
  sep(15);
  text6(16, " APRS RX PAGER");
  text6(24, " STM32F103C8T6");
  text6(32, " ST7567 128x64 LCD");
  text6(40, " BK4802P 21.25MHz");
  text6(48, " FW " UI_FW_VER);
  text6(56, " booting...");
  lcd_flush();
}

static void draw_standby(void)
{
  char buf[36];
  lcd_clear(0);
  text8_center(0, "BBCALL APRS");
  sep(15);

  snprintf(buf, sizeof(buf), "%lu.%03lu MHz",
           (unsigned long)(s_freq_khz / 1000u), (unsigned long)(s_freq_khz % 1000u));
  text6(16, buf);

  snprintf(buf, sizeof(buf), "RX %u", (unsigned)s_rx_total);
  text6(24, buf);
  snprintf(buf, sizeof(buf), "MSG %u", (unsigned)s_count);
  text6_right(24, buf);

  if (s_unread > 0u) snprintf(buf, sizeof(buf), "NEW %u", (unsigned)s_unread);
  else               snprintf(buf, sizeof(buf), "NO NEW MSG");
  text6(32, buf);

  text6(40, "---------------------");

  if (s_count > 0u) {
    const ui_item_t *it = &s_box[s_count - 1u];
    snprintf(buf, sizeof(buf), "%-6.6s %s", it->src, kind_tag(it->kind));
    text6(48, buf);
    text6(56, it->title);
  } else {
    text6(48, "LAST:");
    text6(56, "waiting for APRS...");
  }
  lcd_flush();
}

static void draw_inbox(void)
{
  char line[26];
  char hdr[24];
  uint8_t i;
  lcd_clear(0);
  if (s_count == 0u) {
    text8(0, "INBOX");
    sep(15);
    text6(24, "no message yet");
    text6(56, "waiting for APRS...");
    lcd_flush();
    return;
  }
  if (s_sel >= s_count) s_sel = (uint8_t)(s_count - 1u);
  if (s_sel < s_top) s_top = s_sel;
  if ((uint16_t)(s_top + 5u) < s_sel) s_top = (uint8_t)(s_sel - 5u);
  if ((uint16_t)s_top + 6u > s_count) {
    s_top = (s_count > 6u) ? (uint8_t)(s_count - 6u) : 0u;
  }

  snprintf(hdr, sizeof(hdr), "INBOX %u/%u", (unsigned)(s_sel + 1u), (unsigned)s_count);
  text8(0, hdr);
  if (s_unread > 0u) {
    snprintf(hdr, sizeof(hdr), "%u NEW", (unsigned)s_unread);
    text6_right(4, hdr);
  }
  sep(15);

  for (i = 0; i < 6u; i++) {
    uint8_t idx = (uint8_t)(s_top + i);
    if (idx >= s_count) break;
    snprintf(line, sizeof(line), "%-6.6s %c %-11.11s%c",
             s_box[idx].src,
             kind_char(s_box[idx].kind),
             s_box[idx].title,
             s_box[idx].read ? ' ' : '*');
    row6(ROW6(i), line, (idx == s_sel) ? 1u : 0u);
  }
  lcd_flush();
}

static void draw_detail(void)
{
  char lines[14][22];
  char hdr[24];
  char foot[64];
  const ui_item_t *it;
  uint8_t n, i, pages;

  if (s_count == 0u) { draw_inbox(); return; }
  if (s_sel >= s_count) s_sel = (uint8_t)(s_count - 1u);
  it = &s_box[s_sel];

  n = wrap_text(it->body, lines, 14u, 21u);
  if (n == 0u) { lines[0][0] = 0; n = 1u; }
  pages = (uint8_t)((n + 4u) / 5u);      /* 每页 5 行 */
  if (pages == 0u) pages = 1u;
  if (s_page >= pages) s_page = (uint8_t)(pages - 1u);

  lcd_clear(0);
  snprintf(hdr, sizeof(hdr), "%s %s", kind_tag(it->kind), it->src);
  text8(0, hdr);
  sep(15);

  for (i = 0; i < 5u; i++) {
    uint8_t idx = (uint8_t)(s_page * 5u + i);
    if (idx < n) text6(ROW6(i), lines[idx]);
  }

  snprintf(foot, sizeof(foot), "%u/%u", (unsigned)(s_page + 1u), (unsigned)pages);
  if (it->relay)       { strncat(foot, " RELAY ", sizeof(foot) - strlen(foot) - 1u);
                         strncat(foot, it->path, sizeof(foot) - strlen(foot) - 1u); }
  else if (it->fixed)  strncat(foot, " FIX", sizeof(foot) - strlen(foot) - 1u);
  else if (it->repeat) strncat(foot, " REP", sizeof(foot) - strlen(foot) - 1u);
  text6(ROW6(5), foot);
  lcd_flush();
}

/* 删除确认弹窗：盖在列表/详情中部的反显框 */
static void draw_confirm(void)
{
  lcd_fill_rect(0, 20, 127, 43, 1);
  lcd_draw_string6x8(6, 24, "DELETE THIS MESSAGE?", 0);
  lcd_draw_string6x8(6, 32, "OK=YES   BACK=CANCEL", 0);
  lcd_flush();          /* 弹窗写在底层画面之后，必须再刷一次 */
}

static void redraw(void)
{
  switch (s_scr) {
    case UI_SCREEN_STANDBY: draw_standby(); break;
    case UI_SCREEN_INBOX:   draw_inbox();   break;
    case UI_SCREEN_DETAIL:  draw_detail();  break;
    case UI_SCREEN_BOOT:    draw_boot();    break;
    default:                draw_pattern(); break;
  }
  if (s_confirm) draw_confirm();
}

/* ------------------------------------------------------------------ */
/* 数据注入                                                            */
/* ------------------------------------------------------------------ */
static void delete_sel(void)
{
  if (s_count == 0u) return;
  if (!s_box[s_sel].read) {
    s_box[s_sel].read = 1u;
    if (s_unread > 0u) s_unread--;
  }
  if ((uint16_t)(s_sel + 1u) < s_count) {
    memmove(&s_box[s_sel], &s_box[s_sel + 1u],
            sizeof(ui_item_t) * (size_t)(s_count - s_sel - 1u));
  }
  s_count--;
  if (s_sel >= s_count) s_sel = (s_count > 0u) ? (uint8_t)(s_count - 1u) : 0u;
  if (s_top > s_sel) s_top = s_sel;
  if (s_count <= 6u) s_top = 0u;
  else if ((uint16_t)(s_top + 6u) > s_count) s_top = (uint8_t)(s_count - 6u);
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

  /* Mic-E 的目标呼号含位置模糊度空格，必须用原始地址字节 */
  for (i = 0; i < 6u; i++) {
    uint8_t c = (uint8_t)((frame[i] >> 1) & 0x7Fu);
    mice_dest[i] = (char)((c == 0u) ? ' ' : (char)c);
  }
  mice_dest[6] = 0;

  if (s_count >= UI_INBOX_MAX) {
    if (!s_box[0].read) { if (s_unread > 0u) s_unread--; }
    memmove(&s_box[0], &s_box[1], sizeof(ui_item_t) * (size_t)(UI_INBOX_MAX - 1u));
    s_count = (uint8_t)(UI_INBOX_MAX - 1u);
  }
  it = &s_box[s_count];
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->read = 0u;
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
  s_sel = (uint8_t)(s_count - 1u);
  return 1u;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */
void ui_init(void)
{
  lcd_init();
  s_count = 0u;
  s_sel = 0u;
  s_top = 0u;
  s_page = 0u;
  s_confirm = 0u;
  s_rx_total = 0u;
  s_unread = 0u;
  s_dup_total = 0u;
  s_dup_pos = 0u;
  s_scr = UI_SCREEN_BOOT;
  memset(s_box, 0, sizeof(s_box));
  memset(s_dup_src, 0, sizeof(s_dup_src));
  draw_boot();
}

void ui_show(int screen)
{
  if (screen == UI_SCREEN_CONFIRM) {          /* 自检用：直接弹删除确认 */
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
    if (key == 3) {          /* 确定 -> 真删除 */
      s_confirm = 0u;
      delete_sel();
      s_scr = UI_SCREEN_INBOX;
      redraw();
    } else if (key == 4 || key == 8 || key == 7) {   /* 返回/再按删除/待机 -> 取消 */
      s_confirm = 0u;
      redraw();
    }
    return;
  }

  switch (key) {
    case UI_SCREEN_PATTERN: ui_show(UI_SCREEN_PATTERN); break;
    case UI_SCREEN_INBOX:   ui_show(UI_SCREEN_INBOX);   break;
    case UI_SCREEN_STANDBY: ui_show(UI_SCREEN_STANDBY); break;
    case UI_SCREEN_BOOT:    ui_show(UI_SCREEN_BOOT);    break;

    case 1:   /* 上 */
      if (s_scr == UI_SCREEN_INBOX) {
        if (s_sel > 0u) s_sel--;
        draw_inbox();
      } else if (s_scr == UI_SCREEN_DETAIL) {
        if (s_page > 0u) s_page--;
        else if (s_sel > 0u) s_sel--;
        draw_detail();
      } else {
        ui_show(UI_SCREEN_INBOX);
      }
      break;

    case 2:   /* 下 */
      if (s_scr == UI_SCREEN_INBOX) {
        if ((uint16_t)(s_sel + 1u) < s_count) s_sel++;
        draw_inbox();
      } else if (s_scr == UI_SCREEN_DETAIL) {
        s_page++;
        draw_detail();
      } else {
        ui_show(UI_SCREEN_INBOX);
      }
      break;

    case 3:   /* 确定：打开并把该条标记为已读 */
      if (s_scr == UI_SCREEN_STANDBY) {
        ui_show(UI_SCREEN_INBOX);
      } else if (s_scr == UI_SCREEN_INBOX) {
        if (s_count > 0u && !s_box[s_sel].read) {
          s_box[s_sel].read = 1u;
          if (s_unread > 0u) s_unread--;
        }
        s_page = 0u;
        ui_show(UI_SCREEN_DETAIL);
      }
      break;

    case 4:   /* 返回 */
      if (s_scr == UI_SCREEN_DETAIL) ui_show(UI_SCREEN_INBOX);
      else                           ui_show(UI_SCREEN_STANDBY);
      break;

    case 8:   /* 删除 -> 先弹确认 */
      if ((s_scr == UI_SCREEN_INBOX || s_scr == UI_SCREEN_DETAIL) && s_count > 0u) {
        s_confirm = 1u;
        redraw();
      }
      break;

    default:
      break;
  }
}

void ui_set_rx_freq_khz(uint32_t khz) { s_freq_khz = khz; }
uint16_t ui_inbox_count(void) { return s_count; }
uint16_t ui_unread_count(void) { return s_unread; }
uint16_t ui_rx_total(void) { return s_rx_total; }
uint16_t ui_dup_total(void) { return s_dup_total; }

void ui_tick(uint32_t ms)
{
  s_now_ms += ms;
  (void)s_now_ms;
}