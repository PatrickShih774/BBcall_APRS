/*
 * ST7567 128x64 LCD 驱动（GPIO 位敲 SPI, 4 线：CS/CLK/MOSI + A0 + RST）
 * 帧缓冲：128x64/8 = 1024 字节，按 8 页 x 128 列组织。
 * 中文字体（16x16）需接入字库表，见 tools/gen_font.py 的扩展或外部字库。
 */
#include "lcd_st7567.h"
#include "bbcall_config.h"
#include "font8x16.h"

#include "stm32f1xx_hal.h"
#include <stdlib.h>

static uint8_t fb[LCD_FB_BYTES];

/* ---------- GPIO 宏 ---------- */
#define LCD_CS_HI()   HAL_GPIO_WritePin(LCD_GPIO,   LCD_CS_PIN,      GPIO_PIN_SET)
#define LCD_CS_LO()   HAL_GPIO_WritePin(LCD_GPIO,   LCD_CS_PIN,      GPIO_PIN_RESET)
#define LCD_CLK_HI()  HAL_GPIO_WritePin(LCD_GPIO,   LCD_CLK_PIN,     GPIO_PIN_SET)
#define LCD_CLK_LO()  HAL_GPIO_WritePin(LCD_GPIO,   LCD_CLK_PIN,     GPIO_PIN_RESET)
#define LCD_MOSI_HI() HAL_GPIO_WritePin(LCD_GPIO,   LCD_MOSI_PIN,    GPIO_PIN_SET)
#define LCD_MOSI_LO() HAL_GPIO_WritePin(LCD_GPIO,   LCD_MOSI_PIN,    GPIO_PIN_RESET)
#define LCD_A0_CMD()  HAL_GPIO_WritePin(LCD_GPIO,   LCD_A0_PIN,      GPIO_PIN_RESET)
#define LCD_A0_DAT()  HAL_GPIO_WritePin(LCD_GPIO,   LCD_A0_PIN,      GPIO_PIN_SET)

static void lcd_delay(void){ volatile uint32_t n = 8; while (n--) __asm volatile ("nop"); }

static void lcd_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) LCD_MOSI_HI();
        else               LCD_MOSI_LO();
        lcd_delay();
        LCD_CLK_HI();
        lcd_delay();
        LCD_CLK_LO();
        lcd_delay();
    }
}

static void lcd_cmd(uint8_t c)
{
    LCD_CS_LO();
    LCD_A0_CMD();
    lcd_byte(c);
    LCD_CS_HI();
}

static void lcd_cmd2(uint8_t c1, uint8_t c2)
{
    LCD_CS_LO();
    LCD_A0_CMD();
    lcd_byte(c1);
    lcd_byte(c2);
    LCD_CS_HI();
}

static void lcd_data(const uint8_t *d, uint16_t n)
{
    LCD_CS_LO();
    LCD_A0_DAT();
    for (uint16_t i = 0; i < n; i++) lcd_byte(d[i]);
    LCD_CS_HI();
}

/* ---------- 初始化 （不同 ST7567 模组可能略有差异，可按需微调序列） ---------- */
void lcd_init(void)
{
    GPIO_InitTypeDef gout = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gout.Pin   = LCD_CS_PIN | LCD_CLK_PIN | LCD_MOSI_PIN | LCD_A0_PIN;
    gout.Mode  = GPIO_MODE_OUTPUT_PP;
    gout.Pull  = GPIO_NOPULL;
    gout.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LCD_GPIO, &gout);

    gout.Pin  = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_GPIO, &gout);

    gout.Pin  = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_GPIO, &gout);

    LCD_CS_HI();

    /* 复位 */
    HAL_GPIO_WritePin(LCD_GPIO, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_GPIO, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(LCD_GPIO, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);

    lcd_cmd(0xE2);          /* 软复位 */
    lcd_cmd(0xAE);          /* 显示关 */
    lcd_cmd(0x40);          /* 起始行 0 */
    lcd_cmd(0xA0);          /* 段方向：0xA0 正常 / 0xA1 左右镜像 */
    lcd_cmd(0xC0);          /* COM 方向 */
    lcd_cmd(0xA6);          /* 正常显示 */
    lcd_cmd(0xA2);          /* 偏压设置（1/9） */
    lcd_cmd(0x2C);          /* 电源控制 */
    lcd_cmd(0x25);          /* 调节电阻 */
    lcd_cmd(0x81);          /* 对比度 */
    lcd_cmd(0x1C);
    lcd_cmd(0xA4);          /* 正常（非全部点亮） */
    lcd_cmd(0xAF);          /* 显示开 */

    lcd_clear(0);
    lcd_flush();
    lcd_backlight(1);
}

/* ---------- 帧缓冲操作 ---------- */
void lcd_clear(uint8_t color)
{
    for (uint32_t i = 0; i < LCD_FB_BYTES; i++) fb[i] = color ? 0xFF : 0x00;
}

void lcd_pixel(uint8_t x, uint8_t y, uint8_t on)
{
    if (x >= LCD_W || y >= LCD_H) return;
    uint8_t page = y / 8;
    uint8_t bit  = y % 8;
    uint8_t mask = (uint8_t)(1u << bit);
    if (on) fb[page * LCD_W + x] |= mask;
    else    fb[page * LCD_W + x] &= (uint8_t)~mask;
}

void lcd_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on)
{
    int dx = (int)x1 - x0, dy = (int)y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = abs(dx); dy = abs(dy);
    int err = dx - dy;
    for (;;) {
        lcd_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void lcd_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on)
{
    lcd_line(x, y, x + w, y, on);
    lcd_line(x, y + h, x + w, y + h, on);
    lcd_line(x, y, x, y + h, on);
    lcd_line(x + w, y, x + w, y + h, on);
}

/* ---------- 文本 ---------- */
void lcd_draw_char8x16(uint8_t x, uint8_t y, uint8_t ch, uint8_t on)
{
    if (ch < 0x20) ch = 0x20;
    if (ch > 0x7F) ch = 0x20;
    const uint8_t *g = font8x16[ch - 0x20];
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 8; c++) {
            if (g[r] & (0x80 >> c)) lcd_pixel(x + c, y + r, on);
        }
    }
}

void lcd_draw_string8x16(uint8_t x, uint8_t y, const char *s, uint8_t on)
{
    uint8_t cx = x;
    while (*s && cx + 8 <= LCD_W) {
        lcd_draw_char8x16(cx, y, (uint8_t)*s, on);
        cx += 8;
        s++;
    }
}

/* 16x16 中文字形（子集字库）；glyph[32] 顺序：前 16 字节为左半16点，后 16 字节为右半16点 */
void lcd_draw_chinese16x16(uint8_t x, uint8_t y, const uint8_t glyph[32], uint8_t on)
{
    for (int r = 0; r < 16; r++) {
        uint8_t l = glyph[r];
        uint8_t rr = glyph[16 + r];
        for (int c = 0; c < 8; c++) {
            if (l & (0x80 >> c))  lcd_pixel(x + c, y + r, on);
            if (rr & (0x80 >> c)) lcd_pixel(x + 8 + c, y + r, on);
        }
    }
}

/* ---------- 刷新到 LCD ---------- */
void lcd_flush(void)
{
    for (uint8_t page = 0; page < LCD_PAGES; page++) {
        lcd_cmd(0xB0 | page);          /* 页地址 */
        lcd_cmd(0x00);                  /* 列地址低 4 位 */
        lcd_cmd(0x10);                  /* 列地址高 4 位 */
        lcd_data(&fb[page * LCD_W], LCD_W);
    }
}

void lcd_backlight(uint8_t on)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO, LCD_BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
