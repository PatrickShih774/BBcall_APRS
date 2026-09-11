/*
 * 模拟器 UI harness
 *
 * 复刻 BB 机的待机 / 收件箱 / 详情 / 删除交互；数据来自真实 AX.25 帧
 * （串口日志回放或 WAV 解调），解析复用固件 ax25.c / aprs.c，绘图复用 lcd_st7567.c。
 *
 * 屏幕：测试图案 / 待机 / 收件箱 / 详情
 * 按键：↑↓ 选择、Enter 打开、Backspace 返回、Delete 删除、T 图案、M 收件箱、S 待机
 */
#include "ui_harness.h"
#include "lcd_st7567.h"
#include "ax25.h"
#include "aprs.h"
#include <string.h>
#include <stdio.h>

typedef struct {
  uint8_t  used;
  uint8_t  kind;        /* UI_KIND_* */
  uint8_t  fixed;       /* [FIX]/[FIX2] 纠错恢复 */
  uint8_t  repeat;      /* [REP] 参考帧恢复 */
  uint8_t  relay;       /* 含中继路径 */
  uint8_t  has_time;    /* 日志/回放提供了时间戳 */
  uint16_t seq;         /* 本次会话内的接收序号 */
  char     src[10];
  char     dst[10];
  char     path[30];
  char     title[34];   /* 列表摘要 */
  char     body[UI_BODY_MAX];
  uint32_t rx_ms;
} ui_item_t;

static ui_item_t s_box[UI_INBOX_MAX];
static uint8_t   s_count;
static uint8_t   s_sel;
static uint8_t   s_top;
static uint8_t   s_scr = UI_SCREEN_PATTERN;
static uint8_t   s_page;
static uint16_t  s_rx_total;
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

/* 贪心折行：优先在空格断开，单词过长则硬折 */
static uint8_t wrap_text(const char *s, char lines[][17], uint8_t maxlines)
{
  uint8_t n = 0;
  while (*s && n < maxlines) {
    const char *p = s;
    uint8_t len = 0, last_sp = 0;
    while (*p == ' ') p++;
    if (!*p) break;
    while (p[len] && len < 16u) {
      if (p[len] == ' ') last_sp = len;
      len++;
    }
    if (p[len] && last_sp > 0u) len = last_sp;   /* 在空格处换行 */
    if (len == 0u) break;
    memcpy(lines[n], p, len);
    lines[n][len] = 0;
    s = p + len;
    n++;
  }
  return n;
}

static const char *kind_char(uint8_t kind)
{
  if (kind == UI_KIND_MSG)  return "M";
  if (kind == UI_KIND_POS)  return "P";
  if (kind == UI_KIND_MICE) return "C";
  return "X";
}

static const char *kind_tag(uint8_t kind)
{
  if (kind == UI_KIND_MSG)  return "MSG";
  if (kind == UI_KIND_POS)  return "POS";
  if (kind == UI_KIND_MICE) return "POS";
  return "APRS";
}

static void draw_hline(uint8_t y) { lcd_line(0, y, 127, y, 1); }

/* ------------------------------------------------------------------ */
/* 屏幕                                                                */
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
  lcd_draw_string8x16(24, 16, "ST7567 SIM", 0);
  lcd_draw_string8x16(32, 36, "128x64 LCD", 0);
  lcd_flush();
}

static void draw_standby(void)
{
  char buf[24];
  lcd_clear(0);
  lcd_draw_string8x16(0, 0, "BBCALL APRS RX", 1);
  draw_hline(15);
  snprintf(buf, sizeof(buf), "%lu.%03lu MHz",
           (unsigned long)(s_freq_khz / 1000u),
           (unsigned long)(s_freq_khz % 1000u));
  lcd_draw_string8x16(0, 16, buf, 1);
  snprintf(buf, sizeof(buf), "RX=%u MSG=%u", (unsigned)s_rx_total, (unsigned)s_count);
  lcd_draw_string8x16(0, 32, buf, 1);
  if (s_count > 0u) {
    snprintf(buf, sizeof(buf), "%-6.6s %s", s_box[s_count - 1u].src,
             kind_tag(s_box[s_count - 1u].kind));
    lcd_draw_string8x16(0, 48, buf, 1);
  } else {
    lcd_draw_string8x16(0, 48, "WAITING...", 1);
  }
  lcd_flush();
}

static void draw_inbox(void)
{
  char buf[24];
  uint8_t i;
  lcd_clear(0);
  if (s_count == 0u) {
    lcd_draw_string8x16(0, 0, "INBOX 0", 1);
    draw_hline(15);
    lcd_draw_string8x16(0, 24, "NO MESSAGE", 1);
    lcd_draw_string8x16(0, 48, "S=BACK", 1);
    lcd_flush();
    return;
  }
  if (s_sel >= s_count) s_sel = (uint8_t)(s_count - 1u);
  if (s_sel < s_top) s_top = s_sel;
  if (s_top + 2u < s_sel) s_top = (uint8_t)(s_sel - 2u);
  if ((uint16_t)s_top + 3u > s_count) {
    s_top = (s_count > 3u) ? (uint8_t)(s_count - 3u) : 0u;
  }
  snprintf(buf, sizeof(buf), "INBOX %u/%u", (unsigned)(s_sel + 1u), (unsigned)s_count);
  lcd_draw_string8x16(0, 0, buf, 1);
  draw_hline(15);

  for (i = 0; i < 3u; i++) {
    uint8_t idx = (uint8_t)(s_top + i);
    char line[20];
    if (idx >= s_count) break;
    if (s_box[idx].has_time) {
      snprintf(line, sizeof(line), "%c%-6.6s %s%3us",
               (idx == s_sel) ? '>' : ' ',
               s_box[idx].src,
               kind_char(s_box[idx].kind),
               (unsigned)((s_box[idx].rx_ms / 1000u) % 1000u));
    } else {
      /* 日志没有 [T=..ms] 时退化为会话内序号，避免显示假的 0s */
      snprintf(line, sizeof(line), "%c%-6.6s %s#%02u",
               (idx == s_sel) ? '>' : ' ',
               s_box[idx].src,
               kind_char(s_box[idx].kind),
               (unsigned)(s_box[idx].seq % 100u));
    }
    lcd_draw_string8x16(0, (uint8_t)(16u + i * 16u), line, 1);
  }
  lcd_flush();
}

static void draw_detail(void)
{
  char lines[6][17];
  char buf[24];
  ui_item_t *it;
  uint8_t n, i, pages;

  if (s_count == 0u) { draw_inbox(); return; }
  if (s_sel >= s_count) s_sel = (uint8_t)(s_count - 1u);
  it = &s_box[s_sel];

  n = wrap_text(it->body, lines, 6u);
  if (n == 0u) { lines[0][0] = 0; n = 1u; }
  pages = (uint8_t)((n + 1u) / 2u);
  if (pages == 0u) pages = 1u;
  if (s_page >= pages) s_page = (uint8_t)(pages - 1u);

  lcd_clear(0);
  snprintf(buf, sizeof(buf), "%s %s", kind_tag(it->kind), it->src);
  lcd_draw_string8x16(0, 0, buf, 1);
  draw_hline(15);

  for (i = 0; i < 2u; i++) {
    uint8_t idx = (uint8_t)(s_page * 2u + i);
    if (idx < n) lcd_draw_string8x16(0, (uint8_t)(16u + i * 16u), lines[idx], 1);
  }

  snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(s_page + 1u), (unsigned)pages);
  lcd_draw_string8x16(0, 48, buf, 1);
  if (it->fixed)       lcd_draw_string8x16(96, 48, "FIX", 1);
  else if (it->repeat) lcd_draw_string8x16(96, 48, "REP", 1);
  else if (it->relay) lcd_draw_string8x16(56, 48, "RELAY", 1);
  lcd_flush();
}

/* ------------------------------------------------------------------ */
/* 数据注入                                                            */
/* ------------------------------------------------------------------ */
static void delete_sel(void)
{
  if (s_count == 0u) return;
  if ((uint16_t)(s_sel + 1u) < s_count) {
    memmove(&s_box[s_sel], &s_box[s_sel + 1u],
            sizeof(ui_item_t) * (size_t)(s_count - s_sel - 1u));
  }
  s_count--;
  if (s_sel >= s_count) s_sel = (s_count > 0u) ? (uint8_t)(s_count - 1u) : 0u;
  if (s_top > s_sel) s_top = s_sel;
  if (s_count <= 3u) s_top = 0u;
  else if ((uint16_t)(s_top + 3u) > s_count) s_top = (uint8_t)(s_count - 3u);
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
  uint8_t i;

  if (!ax25_decode(frame, len, &d)) return 0u;

  {
    uint16_t fh = 0;
    for (i = 0; i < len; i++) fh = (uint16_t)((fh << 5) ^ (fh >> 2) ^ frame[i]);
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
    memmove(&s_box[0], &s_box[1], sizeof(ui_item_t) * (size_t)(UI_INBOX_MAX - 1u));
    s_count = (uint8_t)(UI_INBOX_MAX - 1u);
  }
  it = &s_box[s_count];
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->fixed = fixed;
  it->repeat = repeat;
  it->rx_ms = t_ms;
  it->has_time = (t_ms > 0u) ? 1u : 0u;
  it->seq = s_rx_total;
  clip(it->src, sizeof(it->src), d.src);
  clip(it->dst, sizeof(it->dst), d.dest);

  /* 中继路径 */
  it->relay = (d.npath > 0u) ? 1u : 0u;
  if (d.npath == 0u) {
    clip(it->path, sizeof(it->path), "-");
  } else {
    char tmp[44];
    uint8_t n = 0;
    for (i = 0; i < d.npath && (uint8_t)(n + 9u) < sizeof(tmp); i++) {
      uint8_t k;
      for (k = 0; k < 6u && d.path[i][k]; k++) tmp[n++] = (char)d.path[i][k];
      if (d.path_h[i]) tmp[n++] = '*';
      if ((uint8_t)(i + 1u) < d.npath) tmp[n++] = ',';
    }
    tmp[n] = 0;
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
    {
      char body[UI_BODY_MAX];
      uint8_t n = (uint8_t)((m.body_len < (UI_BODY_MAX - 1u)) ? m.body_len : (UI_BODY_MAX - 1u));
      for (i = 0; i < n; i++) {
        uint8_t c = m.body[i];
        body[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
      }
      body[n] = 0;
      clip(it->body, sizeof(it->body), body);
      clip(it->title, sizeof(it->title), body);
    }
  } else {
    char body[UI_BODY_MAX];
    uint8_t n = (uint8_t)((d.info_len < (UI_BODY_MAX - 1u)) ? d.info_len : (UI_BODY_MAX - 1u));
    it->kind = UI_KIND_OTHER;
    for (i = 0; i < n; i++) {
      uint8_t c = d.info[i];
      body[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
    }
    body[n] = 0;
    clip(it->body, sizeof(it->body), body);
    clip(it->title, sizeof(it->title), body);
  }

  it->seq = s_rx_total;
  s_count++;
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
  s_rx_total = 0u;
  s_dup_total = 0u;
  s_dup_pos = 0u;
  memset(s_dup_src, 0, sizeof(s_dup_src));
  s_scr = UI_SCREEN_PATTERN;
  memset(s_box, 0, sizeof(s_box));
  draw_pattern();
}

void ui_show(int screen)
{
  s_scr = (uint8_t)screen;
  s_page = 0u;
  switch (s_scr) {
    case UI_SCREEN_STANDBY: draw_standby(); break;
    case UI_SCREEN_INBOX:   draw_inbox();   break;
    case UI_SCREEN_DETAIL:  draw_detail();  break;
    default:                draw_pattern(); break;
  }
}

void ui_handle_key(int key)
{
  switch (key) {
    case UI_SCREEN_PATTERN: ui_show(UI_SCREEN_PATTERN); break;
    case UI_SCREEN_INBOX:   ui_show(UI_SCREEN_INBOX);   break;
    case UI_SCREEN_STANDBY: ui_show(UI_SCREEN_STANDBY); break;

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

    case 3:   /* 确定 */
      if (s_scr == UI_SCREEN_INBOX || s_scr == UI_SCREEN_STANDBY) {
        s_page = 0u;
        ui_show(UI_SCREEN_DETAIL);
      }
      break;

    case 4:   /* 返回 */
      if (s_scr == UI_SCREEN_DETAIL) ui_show(UI_SCREEN_INBOX);
      else                           ui_show(UI_SCREEN_STANDBY);
      break;

    case 8:   /* 删除 */
      if (s_scr == UI_SCREEN_INBOX || s_scr == UI_SCREEN_DETAIL) {
        delete_sel();
        ui_show(UI_SCREEN_INBOX);
      }
      break;

    default:
      break;
  }
}

void ui_set_rx_freq_khz(uint32_t khz) { s_freq_khz = khz; }
uint16_t ui_inbox_count(void) { return s_count; }
uint16_t ui_rx_total(void) { return s_rx_total; }
uint16_t ui_dup_total(void) { return s_dup_total; }

void ui_tick(uint32_t ms)
{
  s_now_ms += ms;
  (void)s_now_ms;
}