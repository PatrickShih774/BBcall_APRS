#ifndef LCD_SIM_H
#define LCD_SIM_H
#include <stdint.h>

#define SIM_KEY_UP       1
#define SIM_KEY_DOWN     2
#define SIM_KEY_OK       3
#define SIM_KEY_BACK     4
#define SIM_KEY_PATTERN  5
#define SIM_KEY_MESSAGE  6
#define SIM_KEY_STANDBY  7
#define SIM_KEY_DEL      8
#define SIM_KEY_MENU     9

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

/* 供 lcd_st7567.c 的 LCD_SIM 分支调用 */
void lcd_sim_reset(void);
void lcd_sim_cmd(uint8_t c);
void lcd_sim_data(uint8_t d);
void lcd_sim_data_bytes(const uint8_t *d, uint16_t n);
void lcd_sim_backlight(uint8_t on);
#endif