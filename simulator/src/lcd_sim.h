#ifndef LCD_SIM_H
#define LCD_SIM_H
#include <stdint.h>
#include "ui_harness.h"   /* SIM_KEY_* 键码（设备层定义） */

/* 键盘映射：↑ ↓ Enter；Enter 按住 ≥620ms 松开后发 SIM_KEY_OK_LONG（design.md §9）。 */

int  lcd_sim_init(const char *title, int scale);
void lcd_sim_shutdown(void);
void lcd_sim_poll_events(void);
int  lcd_sim_should_quit(void);
void lcd_sim_render(void);
void lcd_sim_save_bmp(const char *path);
void lcd_sim_toggle_invert(void);
void lcd_sim_toggle_backlight(void);
void lcd_sim_toggle_panel(void);
int  lcd_sim_get_key(void);
/* SDL 键码 -> 内部键码（0 表示无映射）。抽成纯函数以便自检。 */
int  lcd_sim_map_key(int sdl_sym);

/* 供 lcd_st7567.c 的 LCD_SIM 分支调用 */
void lcd_sim_reset(void);
void lcd_sim_cmd(uint8_t c);
void lcd_sim_data(uint8_t d);
void lcd_sim_data_bytes(const uint8_t *d, uint16_t n);
void lcd_sim_backlight(uint8_t on);
#endif