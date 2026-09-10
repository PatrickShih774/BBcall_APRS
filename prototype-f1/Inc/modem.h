#ifndef MODEM_H
#define MODEM_H

#include "ax25.h"

typedef enum { AFSK_NONE = 0, AFSK_MARK = 1, AFSK_SPACE = 2 } afsk_tone_t;

/* 1200 baud AFSK 参数 */
#define AFSK_BIT_US     833               /* 位周期 us */
#define AFSK_MARK_HZ    1200
#define AFSK_SPACE_HZ   2200

void modem_init(void);

/* 定时器输入捕获 ISR 调用：给出相邻两次零交叉的周期 */
void modem_on_capture_period(uint16_t period_us);

/* 定时器（如 SysTick/定时器）周期采样：按位时钟 mid-bit 采样，输出数据比特到 HDLC */
void modem_sample(void);

/* 复位同步状态（换频后调用） */
void modem_reset_sync(void);

#endif /* MODEM_H */
