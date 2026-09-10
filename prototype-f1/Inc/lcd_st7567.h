#ifndef LCD_ST7567_H
#define LCD_ST7567_H

#include <stdint.h>

#define LCD_W 128
#define LCD_H 64
#define LCD_PAGES (LCD_H / 8)
#define LCD_FB_BYTES (LCD_W * LCD_PAGES)

void lcd_init(void);
void lcd_clear(uint8_t color);
void lcd_flush(void);

void lcd_pixel(uint8_t x, uint8_t y, uint8_t on);
void lcd_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on); /* Bresenham */
void lcd_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on);

/* 8x16 ASCII, ch=0x20..0x7F */
void lcd_draw_char8x16(uint8_t x, uint8_t y, uint8_t ch, uint8_t on);
void lcd_draw_string8x16(uint8_t x, uint8_t y, const char *s, uint8_t on);

/* 画一个 16x16 中文字形（由字体表提供 32 字节, 顺序：左16高? 见实现注释） */
void lcd_draw_chinese16x16(uint8_t x, uint8_t y, const uint8_t glyph[32], uint8_t on);

void lcd_backlight(uint8_t on);

#endif /* LCD_ST7567_H */
