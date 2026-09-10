/*
 * 1200 baud Bell202 AFSK 解调（ADC 版）
 *
 * PA1 = ADC1_IN1，由 TIM3 以 9600Hz（1200 baud × 8）采样。
 * 每个采样点对最近 8 点做 1200/2200Hz 定点相关（Goertzel），
 * 比较两路幅度判音调，比数过零更抗衰减和噪声。
 * 再按采样相位分成 8 路并行 NRZI + HDLC 解码，CRC 正确的帧才入邮箱。
 * 这样不需要外部比较器，也不依赖单个周期捕获。
 */
#include "modem.h"
#include <string.h>

#define MODEM_NPHASE 8u

typedef struct {
  uint8_t have;
  uint8_t prev_tone;
  ax25_hdlc_t hdlc;
} modem_dec_t;

static modem_dec_t s_dec[MODEM_NPHASE];
static ax25_frame_t s_frame;
static uint8_t s_frame_ready = 0;

static int16_t s_ring[MODEM_NPHASE];   /* 最近 8 个去直流样本 */
static uint32_t s_nsamp = 0;           /* 累计采样数 */
static int32_t s_dc = 0;               /* 直流（VDD/2 偏置）跟踪 */
static uint16_t s_last_adc = 0;
static uint8_t s_last_tone = 0;        /* 0=无 1=mark 2=space */
static uint16_t s_adc_min = 0xFFFFu;
static uint16_t s_adc_max = 0;
static uint16_t s_mark_hits = 0;
static uint16_t s_space_hits = 0;
static uint16_t s_other_hits = 0;

/* 8 点 × 1200/2200Hz 的 cos/sin 表，缩放 64 倍（9600Hz 采样） */
static const int8_t K_COS1[8] = { 64,  45,   0, -45, -64, -45,   0,  45 };
static const int8_t K_SIN1[8] = {  0,  45,  64,  45,   0, -45, -64, -45 };
static const int8_t K_COS2[8] = { 64,   8, -62, -24,  55,  39, -45, -51 };
static const int8_t K_SIN2[8] = {  0,  63,  17, -59, -32,  51,  45, -39 };

static void dec_reset(modem_dec_t *d)
{
  d->have = 0;
  d->prev_tone = 0;
  ax25_hdlc_init(&d->hdlc);
}

void modem_init(void)
{
  modem_reset_sync();
}

void modem_reset_sync(void)
{
  for (uint32_t i = 0; i < MODEM_NPHASE; i++) dec_reset(&s_dec[i]);
  memset(s_ring, 0, sizeof(s_ring));
  s_nsamp = 0;
  s_dc = 0;
  s_last_adc = 0;
  s_last_tone = 0;
  s_frame_ready = 0;
  s_mark_hits = 0;
  s_space_hits = 0;
  s_other_hits = 0;
}

void modem_adc_sample(uint16_t adc)
{
  s_last_adc = adc;
  if (adc < s_adc_min) s_adc_min = adc;
  if (adc > s_adc_max) s_adc_max = adc;

  /* 一阶直流跟踪：去掉 VDD/2 偏置（1.5V 约 1860 码值） */
  s_dc += ((int32_t)adc - s_dc) >> 4;
  int16_t x = (int16_t)((int32_t)adc - s_dc);

  s_ring[s_nsamp & (MODEM_NPHASE - 1u)] = x;
  s_nsamp++;
  if (s_nsamp < MODEM_NPHASE) return;

  /* 最近 8 点与 1200/2200Hz 做相关，比较幅度 */
  int32_t i1 = 0, q1 = 0, i2 = 0, q2 = 0;
  for (uint8_t i = 0; i < MODEM_NPHASE; i++) {
    int16_t xv = s_ring[(uint16_t)(s_nsamp - MODEM_NPHASE + i) & (MODEM_NPHASE - 1u)];
    i1 += (int32_t)xv * K_COS1[i];
    q1 += (int32_t)xv * K_SIN1[i];
    i2 += (int32_t)xv * K_COS2[i];
    q2 += (int32_t)xv * K_SIN2[i];
  }
  int32_t m1 = (i1 < 0 ? -i1 : i1) + (q1 < 0 ? -q1 : q1);
  int32_t m2 = (i2 < 0 ? -i2 : i2) + (q2 < 0 ? -q2 : q2);
  if ((m1 + m2) < 20000) { s_other_hits++; return; }   /* 幅度太低，判为噪声 */

  uint8_t tone = (m1 >= m2) ? 0u : 1u;   /* 0=1200Hz mark, 1=2200Hz space */
  if (tone == 0u) s_mark_hits++; else s_space_hits++;
  s_last_tone = (uint8_t)(tone + 1u);

  /* 第 (n mod 8) 路解码器每 8 个采样得到一次判决（1200 baud） */
  modem_dec_t *d = &s_dec[(s_nsamp - 1u) & (MODEM_NPHASE - 1u)];
  if (!d->have) {
    d->have = 1;
    d->prev_tone = tone;
    return;
  }

  uint8_t bit = (tone == d->prev_tone) ? 1u : 0u;  /* NRZI: 不变=1, 跳变=0 */
  d->prev_tone = tone;

  ax25_frame_t f;
  if (ax25_hdlc_feed_bit(&d->hdlc, bit, &f)) {
    /* 只有 CRC 正确的帧才入邮箱，滤掉 8 路错误相位产生的垃圾 */
    if (ax25_check_frame(f.frame, f.len)) {
      memcpy(&s_frame, &f, sizeof(f));
      s_frame_ready = 1;
    }
  }
}

/* ---------- 旧 TIM2 捕获接口（保留以兼容调用，不再产生数据） ---------- */
void modem_on_capture_period(uint16_t period_us) { (void)period_us; }
void modem_sample(void) { }

uint16_t modem_last_period(void) { return s_last_adc; }  /* 诊断：最近 ADC 原始值 */
uint8_t modem_tone_now(void) { return s_last_tone; }

void modem_get_adc_range(uint16_t *min, uint16_t *max)
{
  if (min) *min = s_adc_min;
  if (max) *max = s_adc_max;
  s_adc_min = 0xFFFFu;
  s_adc_max = 0;
}

void modem_get_stats(uint16_t *mark, uint16_t *space, uint16_t *other)
{
  if (mark)  *mark = s_mark_hits;
  if (space) *space = s_space_hits;
  if (other) *other = s_other_hits;
}

uint8_t modem_get_frame(ax25_frame_t *out)
{
  if (!s_frame_ready || !out) return 0;
  memcpy(out, &s_frame, sizeof(s_frame));
  s_frame_ready = 0;
  return 1;
}
