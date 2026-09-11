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
/* 6x8 小字号：每行 21 字符、每屏 8 行，用于列表/正文 */
void lcd_draw_char6x8(uint8_t x, uint8_t y, uint8_t ch, uint8_t on);
void lcd_draw_string6x8(uint8_t x, uint8_t y, const char *s, uint8_t on);
/* 实心矩形：用于反显选中行 / 弹窗底纹 */
void lcd_fill_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on);
/* 1px 细线与空心矩形：设备 UI 靠细线分隔，不用卡片盒 */
void lcd_hline(uint8_t x0, uint8_t x1, uint8_t y, uint8_t on);
void lcd_vline(uint8_t x, uint8_t y0, uint8_t y1, uint8_t on);
void lcd_rect(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t on);
/* 放大绘制：scale=2 时 8x16 字形变成 16x32 大字（寻呼机大时钟） */
void lcd_draw_char8x16_scaled(uint8_t x, uint8_t y, uint8_t ch, uint8_t on, uint8_t scale);
void lcd_draw_string8x16_scaled(uint8_t x, uint8_t y, const char *s, uint8_t on, uint8_t scale);
void lcd_backlight(uint8_t on);
#endif
