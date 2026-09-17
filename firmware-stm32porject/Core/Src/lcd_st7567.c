/*
 * ST7567 128x64 LCD 驱动（GPIO 位敲 SPI：CS/CLK/MOSI + A0 + RST）
 * 帧缓冲 1024 字节（8 页 x 128 列），UI 通过绘图原语和 fusion_font.h 渲染。
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

static uint8_t fb[LCD_FB_BYTES];

/* 上电白屏自检（1=开，0=关）：见 lcd_init() 末尾说明 */
#ifndef LCD_BOOT_FLASH
#define LCD_BOOT_FLASH 1
#endif

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
  /* 段/行方向：0xA0=SEG 正常(0xA1 反向)，0xC0=COM 正常(0xC8 反向)。
   * 实板结论：本机模组的 SEG 走线是反的（正装要 0xA1）；
   * 面板 180° 安装等于上下+左右一起翻，所以两处同时反向（0xA0 + 0xC8），
   * 列起点也随 ADC 方向换到另一端（见 bbcall_cfg.h 的 LCD_COL_OFFSET）。
   * 模拟器在 lcd_sim.c 里按同一块屏 + 同一安装方向建模，预览与实机一致。 */
#if LCD_MOUNT_180
  lcd_cmd(0x40); lcd_cmd(0xA0); lcd_cmd(0xC8);
#else
  lcd_cmd(0x40); lcd_cmd(0xA1); lcd_cmd(0xC0);
#endif
  lcd_cmd(0xA6); lcd_cmd(0xA2);
  /* 电源控制命令是 0x28 | (VC<<2) | (VR<<1) | VF：VC=电压转换、VR=稳压、VF=电压跟随。
   * 原代码只写 0x2C，等于"只开电压转换"，稳压和电压跟随都没开，V0 建立不起来，
   * 现象就是"背光亮、屏上一个字都没有"。按 2C -> 2E -> 2F 逐级打开，给电荷泵留建立时间。 */
  lcd_cmd(0x2C); HAL_Delay(2);      /* VC on */
  lcd_cmd(0x2E); HAL_Delay(2);      /* VC + VR on */
  lcd_cmd(0x2F); HAL_Delay(2);      /* VC + VR + VF on：这一步之后 V0 才到位 */
  lcd_cmd(0x25);                    /* 内部电阻比 5（0x20~0x27） */
  lcd_cmd2(0x81, 0x12);             /* 电子音量（对比度）0x12 = 0x24 的一半；范围 0x00~0x3F，太浓往下减、太淡往上加 */
  lcd_cmd(0xA4); lcd_cmd(0xAF);

#if LCD_BOOT_FLASH
  /* 上电全屏点亮 300ms 再清屏：分诊用。看不到这一下全白，问题就在硬件侧
   * （PSB 串/并口选择、CS/RST 接线、V0 电容、对比度），而不是界面画错。
   * 不想看到就把宏改成 0。 */
  lcd_clear(1);
  lcd_flush();
  HAL_Delay(300);
#endif
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

void lcd_flush(void)
{
  for (uint8_t page = 0; page < LCD_PAGES; page++) {
    lcd_cmd(0xB0u | page);
    /* 列地址起点 = LCD_COL_OFFSET：本机模组可见 SEG 从芯片第 4 列开始（见 bbcall_cfg.h），
     * 于是整幅画面右移 4 像素。芯片列地址 0..131 自增，写满 128 字节不会回卷到左侧。 */
    lcd_cmd((uint8_t)(0x10u | ((LCD_COL_OFFSET >> 4) & 0x0Fu)));
    lcd_cmd((uint8_t)(LCD_COL_OFFSET & 0x0Fu));
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