#ifndef UI_HARNESS_H
#define UI_HARNESS_H
#include <stdint.h>

/* 收件箱条目类型 */
#define UI_KIND_MSG   0
#define UI_KIND_POS   1
#define UI_KIND_MICE  2
#define UI_KIND_OTHER 3

#define UI_INBOX_MAX 24
#define UI_BODY_MAX  128

/* 屏幕编号（--screen 用；与按键编号解耦） */
#define UI_SCREEN_PATTERN 5
#define UI_SCREEN_INBOX   6
#define UI_SCREEN_HOME    7     /* 主页（旧称 standby） */
#define UI_SCREEN_DETAIL  10
#define UI_SCREEN_BOOT    11
#define UI_SCREEN_CONFIRM 12    /* 删除确认弹窗（叠在列表上） */
/* Messenger：只保留收件箱与阅读（本项目仅接收，不做组包/已发） */
#define UI_SCREEN_MSG_INBOX   21
#define UI_SCREEN_MSG_READ    23
#define UI_SCREEN_CNFONT      30   /* 中文字库样张 */
#define UI_SCREEN_MENU    13
#define UI_SCREEN_RADIO   14
#define UI_SCREEN_ABOUT   15
#define UI_SCREEN_STANDBY UI_SCREEN_HOME   /* 兼容旧名 */

void     ui_init(void);
void     ui_handle_key(int key);
void     ui_tick(uint32_t ms);
void     ui_show(int screen);

void     ui_set_rx_freq_khz(uint32_t khz);
void     ui_set_smeter(uint8_t s);        /* 0..9，状态栏信号格 */
void     ui_set_muted(uint8_t muted);     /* 状态栏静音图标 */
void     ui_set_clock_ms(uint32_t ms);    /* 主页大时钟（无 RTC 时为开机计时） */
void     ui_set_radio_stats(uint16_t rssi, uint16_t snr, uint16_t afc, uint16_t exn);

/* 注入一帧 CRC 正确的 AX.25 帧（来自 modem 解调或串口日志回放） */
uint8_t  ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                      uint8_t fixed, uint8_t repeat);

uint16_t ui_inbox_count(void);
uint16_t ui_unread_count(void);
uint16_t ui_rx_total(void);
uint16_t ui_dup_total(void);
#endif