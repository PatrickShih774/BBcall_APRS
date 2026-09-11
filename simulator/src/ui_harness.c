#include "ui_harness.h"
#include "lcd_st7567.h"
#include <string.h>

typedef enum { SCR_PATTERN = 0, SCR_STANDBY, SCR_MESSAGE, SCR_INBOX } screen_t;
static screen_t s_scr = SCR_PATTERN;
static int s_sel = 0;
static uint32_t s_tick = 0;

static const char *s_msg_from = "BG5BLH";
static const char *s_msg_body = "Hello APRS 144.640";

static void draw_pattern(void)
{
  lcd_clear(0);
  for (int y = 0; y < LCD_H; y++)
    for (int x = 0; x < LCD_W; x++)
      if (((x / 4) + (y / 4)) & 1) lcd_pixel((uint8_t)x, (uint8_t)y, 1);
  lcd_line(0, 0, 127, 63, 1);
  lcd_line(127, 0, 0, 63, 1);
  lcd_draw_string8x16(24, 16, "ST7567 SIM", 0);
  lcd_draw_string8x16(32, 36, "128x64 LCD", 0);
  lcd_flush();
}

static void draw_standby(void)
{
  char line[24];
  lcd_clear(0);
  lcd_draw_string8x16(0, 0, "BBCALL APRS RX", 1);
  lcd_line(0, 17, 127, 17, 1);
  lcd_draw_string8x16(0, 22, "144.640 MHz", 1);
  lcd_draw_string8x16(0, 40, "S=09  MSG=01", 1);
  lcd_draw_string8x16(0, 54, "WAITING...", 1);
  (void)line;
  lcd_flush();
}

static void draw_message(void)
{
  lcd_clear(0);
  lcd_draw_string8x16(0, 0, "FROM ", 1);
  lcd_draw_string8x16(40, 0, s_msg_from, 1);
  lcd_line(0, 17, 127, 17, 1);
  lcd_draw_string8x16(0, 24, s_msg_body, 1);
  lcd_draw_string8x16(0, 48, "1/1  OK=BACK", 1);
  lcd_flush();
}

static const char *s_inbox[3] = { "BG5BLH", "BG5AOZ", "BY4SZ" };

static void draw_inbox(void)
{
  lcd_clear(0);
  lcd_draw_string8x16(0, 0, "INBOX", 1);
  lcd_line(0, 17, 127, 17, 1);
  for (int i = 0; i < 3; i++) {
    lcd_draw_string8x16(0, (uint8_t)(20 + i * 14), (i == s_sel) ? ">" : " ", 1);
    lcd_draw_string8x16(10, (uint8_t)(20 + i * 14), s_inbox[i], 1);
  }
  lcd_flush();
}

void ui_init(void)
{
  lcd_init();
  s_scr = SCR_PATTERN;
  s_sel = 0;
  draw_pattern();
}

void ui_handle_key(int key)
{
  switch (key) {
    case 5: s_scr = SCR_PATTERN; draw_pattern(); break;   /* pattern */
    case 6: s_scr = SCR_MESSAGE; draw_message(); break;   /* message */
    case 7: s_scr = SCR_STANDBY; draw_standby(); break;   /* standby */
    case 1: /* up */
      if (s_scr == SCR_INBOX && s_sel > 0) s_sel--;
      else if (s_scr == SCR_STANDBY) { s_scr = SCR_INBOX; draw_inbox(); }
      else if (s_scr == SCR_INBOX) draw_inbox();
      break;
    case 2: /* down */
      if (s_scr == SCR_INBOX && s_sel < 2) s_sel++;
      else if (s_scr == SCR_STANDBY) { s_scr = SCR_INBOX; draw_inbox(); }
      else if (s_scr == SCR_INBOX) draw_inbox();
      break;
    case 3: /* ok */
      if (s_scr == SCR_INBOX || s_scr == SCR_STANDBY) { s_scr = SCR_MESSAGE; draw_message(); }
      break;
    case 4: /* back */
      s_scr = SCR_STANDBY; draw_standby(); break;
    default: break;
  }
}

void ui_tick(uint32_t ms)
{
  s_tick += ms;
  (void)s_tick;
}