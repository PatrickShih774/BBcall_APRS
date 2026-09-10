/*
 * 1200 Baud AFSK (Bell 202) 解调骨架。
 * 原理：定时器捕获零交叉周期 -> 判 mark(1200Hz)/space(2200Hz) -> NRZI(0=跳变,1=不变)
 *       -> 数据比特 -> ax25_hdlc。
 * 阈值/时序为初值，Phase 2 需在实板上用示波器微调（见 PLAN 第 4.2 节）。
 */
#include "modem.h"
#include "aprs.h"
#include <string.h>

static ax25_hdlc_t s_hdlc;
static afsk_tone_t s_tone;        /* 当前周期判出的音调 */
static uint16_t    s_period;      /* 最近一次周期 us */

/* 采样相位：过采样 8x 位时钟，mid-bit 采样 */
#define OVERSAMPLE  8
#define SAMPLE_DIV  (AFSK_BIT_US / OVERSAMPLE)
static uint32_t    s_phase;       /* 0..OVERSAMPLE-1 */
static uint8_t     s_phase_armed; /* 已进入比特窗口 */
static uint8_t     s_prev_tone;

static void modem_emit_bit(void)
{
    uint8_t cur = (s_tone == AFSK_MARK) ? 0 : 1;   /* mark=0, space=1 */
    uint8_t bit = 0;
    if (s_prev_tone != 0xFF) bit = (cur == s_prev_tone) ? 1 : 0;  /* NRZI */
    s_prev_tone = cur;
    ax25_hdlc_feed_bit(&s_hdlc, bit, NULL);
}

void modem_init(void)
{
    ax25_hdlc_init(&s_hdlc);
    modem_reset_sync();
}

void modem_reset_sync(void)
{
    s_tone = AFSK_NONE;
    s_period = 0;
    s_phase = 0;
    s_phase_armed = 0;
    s_prev_tone = 0xFF;
    ax25_hdlc_init(&s_hdlc);
}

void modem_on_capture_period(uint16_t period_us)
{
    s_period = period_us;
    if (period_us >= 700 && period_us <= 950)      s_tone = AFSK_MARK;   /* 1200Hz */
    else if (period_us >= 390 && period_us <= 560) s_tone = AFSK_SPACE;  /* 2200Hz */
    else                                           s_tone = AFSK_NONE;
}

/* 周期采样：应在 OVERSAMPLE(8) x 位速率, 即约 8 次/833us 的定时器上调用 */
void modem_sample(void)
{
    /* 复位相位：当判定到第一个非 NONE 音调时，锁定位时钟起点 */
    if (!s_phase_armed) {
        if (s_tone != AFSK_NONE) { s_phase_armed = 1; s_phase = 0; }
        return;
    }
    /* mid-bit 采样：在相位 OVERSAMPLE/2 处取 */
    if (s_phase == OVERSAMPLE / 2 && s_tone != AFSK_NONE) {
        modem_emit_bit();
    }
    s_phase++;
    if (s_phase >= OVERSAMPLE) s_phase = 0;
}
