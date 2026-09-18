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

/* 收件箱容量与正文字节上限由 RAM 预算定，不是设计常量（design.md 第 9 节明确不锁死容量）。
 * STM32F103C8T6 只有 20KB RAM，而 modem 的 16 相位 + 9 跳变对齐要占 9.8KB：
 * 24 条 x 184 字节的条目会让链接脚本报 region RAM overflowed by 2400 bytes。
 * 16 条 x 144 字节后可链接通过，并留出约 0.3KB 余量。 */
#define UI_INBOX_MAX 16
#define UI_BODY_MAX  64   /* UTF-8 字节；收件箱每行 21 格、最多 4 行，64 字节刚好够两次换行 */

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
 * 入箱成功会立即切到"有未读"页并重绘（design.md 第 9 节：待机 -> 收包 -> 有未读），
 * 不等 ui_tick 的冒号闪烁；已经在收件箱里则不抢焦点。
 * 返回 1 = 入箱，0 = 丢弃（CRC 失败由调用方保证不发生；重复/ack 返回 0）。 */
uint8_t  ui_feed_ax25(const uint8_t *frame, uint16_t len, uint32_t t_ms,
                      uint8_t fixed, uint8_t repeat);

void     ui_set_dedup_ms(uint32_t ms);       /* 重复包去重窗口：只合并"同一次发射"的多路冗余（默认 2000ms，0=不去重） */
uint16_t ui_inbox_count(void);
uint16_t ui_unread_count(void);
uint16_t ui_unread_dropped(void);   /* 满箱且全为未读时被迫丢掉的未读条数（诊断） */
uint16_t ui_rx_total(void);
uint16_t ui_dup_total(void);
uint8_t  ui_current_screen(void); /* 状态机当前页：UI_SCREEN_IDLE/UNREAD/INBOX */
uint32_t ui_clock_ms(void);        /* 当前设备时钟（demo 数据按它回推接收时刻） */
#endif
