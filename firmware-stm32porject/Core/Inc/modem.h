#ifndef MODEM_H
#define MODEM_H
#include "ax25.h"
typedef enum { AFSK_NONE=0, AFSK_MARK=1, AFSK_SPACE=2 } afsk_tone_t;
#define AFSK_BIT_US 833u
void modem_init(void);
/* ADC 版解调：由 TIM3 以 9600Hz 采样后调用 */
void modem_adc_sample(uint16_t adc);
void modem_reset_sync(void);
/* 诊断：最近 ADC 原始值(modem_last_period)与判出的音调（1=MARK, 2=SPACE, 0=无） */
uint16_t modem_last_period(void);
uint8_t modem_tone_now(void);
/* 诊断：取出并清零最近一段的 ADC 最小/最大值 */
void modem_get_adc_range(uint16_t *min, uint16_t *max);
/* 诊断：累计判频次数（mark=1200Hz, space=2200Hz, other=窗口外） */
void modem_get_stats(uint16_t *mark, uint16_t *space, uint16_t *other);
/* 诊断：16 相位路径 / 9 条跳变对齐(TR)路径 各自解出的帧数（含重复） */
void modem_get_path_counts(uint16_t *phase, uint16_t *tr);
/* 拿到解码后的整帧（0=无） */
uint8_t modem_get_frame(ax25_frame_t *out);
uint16_t modem_get_fix_count(void);
uint8_t modem_frame_was_fixed(void);
uint16_t modem_get_fix2_count(void);
uint8_t modem_frame_was_repeat(void);
uint16_t modem_get_rep_count(void);
#endif
