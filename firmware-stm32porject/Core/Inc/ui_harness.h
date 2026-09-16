#ifndef UI_HARNESS_H
#define UI_HARNESS_H
#include <stdint.h>

/* design.md v2.0 三态模型（2026-09-16 重写）：
 *   态 1 待机 UI_SCREEN_IDLE    态 2 有未读 UI_SCREEN_UNREAD  态 3 收件箱 UI_SCREEN_INBOX
 * 另有 UI_SCREEN_PATTERN 为开发用字模/版面自检屏，不属于设备 UI。
 * 按键只有 ▲ ▼ ●（含长按）：SIM_KEY_UP / SIM_KEY_DOWN / SIM_KEY_OK / SIM_KEY_OK_LONG。 */

#define UI_SCREEN_IDLE    1
#define UI_SCREEN_UNREAD  2
#define UI_SCREEN_INBOX   3
#define UI_SCREEN_PATTERN 5

#define UI_INBOX_MAX 24
#define UI_BODY_MAX  96   /* UTF-8 字节；APRS 信息域上限 67 字符，中文按 3 字节计有余量 */

/* design.md §9：设备只有 ▲ ▼ ● 三个键；● 长按(620ms)与短按互斥。
 * 键码定义在设备层（ui_harness），模拟器 lcd_sim 负责把 SDL 事件翻译过来。 */
#define SIM_KEY_UP      1
#define SIM_KEY_DOWN    2
#define SIM_KEY_OK      3
#define SIM_KEY_OK_LONG 4

void     ui_init(void);
void     ui_handle_key(int key);   /* SIM_KEY_*；● 长按与短按互斥（lcd_sim 保证） */
void     ui_tick(uint32_t ms);     /* 推进设备时钟；只在态 1/2 的冒号闪烁处触发重绘 */
void     ui_show(int screen);      /* 自检用：直接渲染某个态（不改变收件箱状态） */

void     ui_set_mycall(const char *call);      /* 本机呼号（态 1 上格）；NULL/空 = NOCALL */
void     ui_set_rx_freq_khz(uint32_t khz);
void     ui_set_clock_ms(uint32_t ms);         /* 设备时钟（自检固定值）：uptime 与日内时刻的来源 */
void     ui_set_wallclock(uint8_t wday, uint8_t mon, uint8_t day);
                                               /* 有 RTC 时态 1 大格第二行显示 周三 9/16；
                                                  * 未设置时回落为开机时长 UP Nd HH:MM（数据诚实，design.md §8.5） */
void     ui_set_batt(int8_t level);            /* -1 无采样(显示 --) / 0 低 / 1 中 / 2 高 */
void     ui_set_radio_stats(int16_t rssi_dbm, int16_t snr);
                                               /* 下一帧入箱时随条目记录（-32768 表示无采样） */

/* 注入一帧 CRC 正确的 AX.25 帧（modem 解调或串口日志回放）。
 * 入箱时捕获当前 RSSI/SNR；ackNNN 送达确认只计数、不进收件箱。
 * 返回 1 = 入箱，0 = 丢弃（CRC 失败由调用方保证不发生；重复/ack 返回 0）。 */
uint8_t  ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                      uint8_t fixed, uint8_t repeat);

uint16_t ui_inbox_count(void);
uint16_t ui_unread_count(void);
uint16_t ui_rx_total(void);
uint16_t ui_dup_total(void);
uint32_t ui_clock_ms(void);        /* 当前设备时钟（demo 数据按它回推接收时刻） */
#endif
