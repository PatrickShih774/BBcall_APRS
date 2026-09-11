#ifndef UI_HARNESS_H
#define UI_HARNESS_H
#include <stdint.h>

/* 收件箱条目类型 */
#define UI_KIND_MSG   0
#define UI_KIND_POS   1
#define UI_KIND_MICE  2
#define UI_KIND_OTHER 3

#define UI_INBOX_MAX 24
#define UI_BODY_MAX  80

/* 屏幕编号（与 lcd_sim 按键编号一致，便于 --screen） */
#define UI_SCREEN_PATTERN 5
#define UI_SCREEN_INBOX   6
#define UI_SCREEN_STANDBY 7
#define UI_SCREEN_DETAIL  10

void     ui_init(void);
void     ui_handle_key(int key);
void     ui_tick(uint32_t ms);
void     ui_set_rx_freq_khz(uint32_t khz);
void     ui_show(int screen);

/* 注入一帧 CRC 正确的 AX.25 帧（来自 modem 解调或串口日志回放） */
uint8_t  ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                      uint8_t fixed, uint8_t repeat);

uint16_t ui_inbox_count(void);
uint16_t ui_rx_total(void);
uint16_t ui_dup_total(void);
#endif