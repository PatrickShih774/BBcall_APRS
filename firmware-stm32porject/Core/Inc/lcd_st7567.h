#ifndef LCD_ST7567_H
#define LCD_ST7567_H
#include <stdint.h>
#define LCD_W 128
#define LCD_H 64
#define LCD_PAGES (LCD_H/8)
#define LCD_FB_BYTES (LCD_W*LCD_PAGES)
void lcd_init(void);
void lcd_clear(uint8_t color);
void lcd_flush(void);
void lcd_pixel(uint8_t x, uint8_t y, uint8_t on);
void lcd_line(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on);
void lcd_draw_char8x16(uint8_t x, uint8_t y, uint8_t ch, uint8_t on);
void lcd_draw_string8x16(uint8_t x, uint8_t y, const char *s, uint8_t on);
void lcd_backlight(uint8_t on);
#endif
