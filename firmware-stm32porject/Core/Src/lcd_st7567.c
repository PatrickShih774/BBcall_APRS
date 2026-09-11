/*
 * ST7567 128x64 LCD 驱动（GPIO 位敲 SPI：CS/CLK/MOSI + A0 + RST）
 * 帧缓冲 1024 字节（8 页 x 128 列），配合 font8x16.h（ASCII 8x16）。
 *
 * PC 模拟器：定义 LCD_SIM 时，命令/数据改走 lcd_sim_*（SDL2 后端），
 * 绘图逻辑、fb、字体完全复用；真机仍走 HAL GPIO。
 */
#ifdef LCD_SIM
#include "sim_hal.h"
#include "lcd_sim.h"
#else
#include "main.h"
#endif
#include "bbcall_cfg.h"
#include "bbcall_hw.h"
#include "lcd_st7567.h"
#include "font8x16.h"
#include "font6x8.h"

static uint8_t fb[LCD_FB_BYTES];

#ifndef LCD_SIM
static void pin_hi(uint16_t p){ HAL_GPIO_WritePin(LCD_GPIO, p, GPIO_PIN_SET); }
static void pin_lo(uint16_t p){ HAL_GPIO_WritePin(LCD_GPIO, p, GPIO_PIN_RESET); }

static void lcd_byte(uint8_t b)
{
  for (int i = 7; i >= 0; i--) {
    if (b & (1u << i)) pin_hi(LCD_MOSI_PIN); else pin_lo(LCD_MOSI_PIN);
    hw_delay_us(1);
    pin_hi(LCD_CLK_PIN);
    hw_delay_us(1);
    pin_lo(LCD_CLK_PIN);
  }
}
#endif

static void lcd_cmd(uint8_t c)
{
#ifdef LCD_SIM
  lcd_sim_cmd(c);
#else
  pin_lo(LCD_CS_PIN);
  pin_lo(LCD_A0_PIN);            /* 命令 */
  lcd_byte(c);
  pin_hi(LCD_CS_PIN);
#endif
}

static void lcd_cmd2(uint8_t c1, uint8_t c2)
{
#ifdef LCD_SIM
  lcd_sim_cmd(c1);
  lcd_sim_cmd(c2);
#else
  pin_lo(LCD_CS_PIN);
  pin_lo(LCD_A0_PIN);
  lcd_byte(c1);
  lcd_byte(c2);
  pin_hi(LCD_CS_PIN);
#endif
}

static void lcd_data_bytes(const uint8_t *d, uint16_t n)
{
#ifdef LCD_SIM
  lcd_sim_data_bytes(d, n);
#else
  pin_lo(LCD_CS_PIN);
  pin_hi(LCD_A0_PIN);            /* 数据 */
  for (uint16_t i = 0; i < n; i++) lcd_byte(d[i]);
  pin_hi(LCD_CS_PIN);
#endif
}

void lcd_init(void)
{
#ifdef LCD_SIM
  lcd_sim_reset();
#else
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin = LCD_CS_PIN | LCD_CLK_PIN | LCD_MOSI_PIN | LCD_A0_PIN | LCD_RST_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LCD_GPIO, &g);

  pin_hi(LCD_CS_PIN);
  /* 复位 */
  pin_hi(LCD_RST_PIN);
  HAL_Delay(5);
  pin_lo(LCD_RST_PIN);
  HAL_Delay(5);
  pin_hi(LCD_RST_PIN);
  HAL_Delay(5);
#endif

  lcd_cmd(0xE2); lcd_cmd(0xAE);
  /* 段方向(SEG/ADC select)：0xA0 正常、0xA1 反向。
   * 原来写的 0xA1 会让文字左右镜像；实测改为 0xA0 后显示正常。
   * COM 方向 0xC0 保持不变，因此只有左右镜像问题。 */
  lcd_cmd(0x40); lcd_cmd(0xA0); lcd_cmd(0xC0);
  lcd_cmd(0xA6); lcd_cmd(0xA2);
  lcd_cmd(0x2C); lcd_cmd(0x25);
  lcd_cmd2(0x81, 0x1C);
  lcd_cmd(0xA4); lcd_cmd(0xAF);

  lcd_clear(0);
  lcd_flush();
  lcd_backlight(1);
}

void lcd_clear(uint8_t color)
{
  for (uint32_t i = 0; i < LCD_FB_BYTES; i++) fb[i] = color ? 0xFFu : 0x00u;
}

void lcd_pixel(uint8_t x, uint8_t y, uint8_t on)
{
  if (x >= LCD_W || y >= LCD_H) return;
  uint8_t page = y / 8;
  uint8_t bit = y % 8;
  uint8_t mask = (uint8_t)(1u << bit);
  if (on) fb[page * LCD_W + x] |= mask;
  else    fb[page * LCD_W + x] &= (uint8_t)~mask;
}

void lcd_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on)
{
  int dx = (int)x1 - (int)x0;
  int dy = (int)y1 - (int)y0;
  int sx = dx < 0 ? -1 : 1;
  int sy = dy < 0 ? -1 : 1;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  int err = dx - dy;
  for (;;) {
    lcd_pixel(x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 = (uint8_t)(x0 + sx); }
    if (e2 < dx)  { err += dx; y0 = (uint8_t)(y0 + sy); }
  }
}

void lcd_draw_char8x16(uint8_t x, uint8_t y, uint8_t ch, uint8_t on)
{
  if (ch < 0x20) ch = 0x20;
  if (ch > 0x7F) ch = 0x20;
  const uint8_t *g = font8x16[ch - 0x20];
  for (int r = 0; r < 16; r++)
    for (int c = 0; c < 8; c++)
      if (g[r] & (0x80u >> c)) lcd_pixel((uint8_t)(x + c), (uint8_t)(y + r), on);
}

void lcd_draw_string8x16(uint8_t x, uint8_t y, const char *s, uint8_t on)
{
  uint8_t cx = x;
  while (*s && (cx + 8) <= LCD_W) {
    lcd_draw_char8x16(cx, y, (uint8_t)*s, on);
    cx += 8;
    s++;
  }
}

void lcd_hline(uint8_t x0, uint8_t x1, uint8_t y, uint8_t on)
{
  uint8_t x, t;
  if (y >= LCD_H) return;
  if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
  if (x1 >= LCD_W) x1 = LCD_W - 1u;
  for (x = x0; x <= x1; x++) lcd_pixel(x, y, on);
}

void lcd_vline(uint8_t x, uint8_t y0, uint8_t y1, uint8_t on)
{
  uint8_t y, t;
  if (x >= LCD_W) return;
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
  if (y1 >= LCD_H) y1 = LCD_H - 1u;
  for (y = y0; y <= y1; y++) lcd_pixel(x, y, on);
}

void lcd_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on)
{
  lcd_hline(x0, x1, y0, on);
  lcd_hline(x0, x1, y1, on);
  lcd_vline(x0, y0, y1, on);
  lcd_vline(x1, y0, y1, on);
}

void lcd_draw_char8x16_scaled(uint8_t x, uint8_t y, uint8_t ch, uint8_t on, uint8_t scale)
{
  const uint8_t *g;
  int r, c, sy, sx;
  if (scale < 1u) scale = 1u;
  if (ch < 0x20u) ch = 0x20u;
  if (ch > 0x7Fu) ch = 0x20u;
  g = font8x16[ch - 0x20u];
  for (r = 0; r < 16; r++) {
    for (c = 0; c < 8; c++) {
      if (!(g[r] & (0x80u >> c))) continue;
      for (sy = 0; sy < scale; sy++)
        for (sx = 0; sx < scale; sx++)
          lcd_pixel((uint8_t)(x + c * scale + sx), (uint8_t)(y + r * scale + sy), on);
    }
  }
}

void lcd_draw_string8x16_scaled(uint8_t x, uint8_t y, const char *s, uint8_t on, uint8_t scale)
{
  uint8_t cx = x;
  if (scale < 1u) scale = 1u;
  while (*s && (uint16_t)cx + 8u * scale <= LCD_W) {
    lcd_draw_char8x16_scaled(cx, y, (uint8_t)*s, on, scale);
    cx = (uint8_t)(cx + 8u * scale);
    s++;
  }
}

void lcd_fill_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on)
{
  uint8_t x, y, t;
  if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
  if (x1 >= LCD_W) x1 = LCD_W - 1u;
  if (y1 >= LCD_H) y1 = LCD_H - 1u;
  for (y = y0; y <= y1; y++)
    for (x = x0; x <= x1; x++) lcd_pixel(x, y, on);
}

void lcd_draw_char6x8(uint8_t x, uint8_t y, uint8_t ch, uint8_t on)
{
  const uint8_t *g;
  int r, c;
  if (ch < 0x20u) ch = 0x20u;
  if (ch > 0x7Fu) ch = 0x20u;
  g = font6x8[ch - 0x20u];
  for (r = 0; r < 8; r++)
    for (c = 0; c < 6; c++)
      if (g[r] & (0x80u >> c)) lcd_pixel((uint8_t)(x + c), (uint8_t)(y + r), on);
}

void lcd_draw_string6x8(uint8_t x, uint8_t y, const char *s, uint8_t on)
{
  uint8_t cx = x;
  while (*s && (uint8_t)(cx + 6u) <= LCD_W) {
    lcd_draw_char6x8(cx, y, (uint8_t)*s, on);
    cx = (uint8_t)(cx + 6u);
    s++;
  }
}

void lcd_flush(void)
{
  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    lcd_cmd(0xB0u | page);
    lcd_cmd(0x00u);
    lcd_cmd(0x10u);
    lcd_data_bytes(&fb[page * LCD_W], LCD_W);
  }
}

void lcd_backlight(uint8_t on)
{
#ifdef LCD_SIM
  lcd_sim_backlight(on);
#else
  HAL_GPIO_WritePin(LCD_BL_GPIO, LCD_BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}