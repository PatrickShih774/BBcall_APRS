/*
 * 1200 baud Bell202 AFSK 解调（ADC 版）
 *
 * PA1 = ADC1_IN1，由 TIM3 以 9600Hz（1200 baud × 8）采样。
 * 每个采样点对最近 8 点做 1200/2200Hz 定点相关（Goertzel），比较幅度判音调。
 * 为覆盖比特边界落在任意半采样点的情况，用相邻采样插值出半采样相位，
 * 共 16 路并行 NRZI + HDLC 解码；只有 CRC 正确的帧才入邮箱。
 *
 * 幅度门限只在极弱（m1+m2<500）时才丢弃，避免弱信号被误判成噪声
 * （原门限 20000 会丢掉大量有效比特，导致成功率很低）。
 */
#include "modem.h"
#include <string.h>

#define MODEM_NPHASE 16u

typedef struct {
  uint8_t have;
  uint8_t prev_tone;
  int32_t acc;
  ax25_hdlc_t hdlc;
} modem_dec_t;

static modem_dec_t s_dec[MODEM_NPHASE];
#define MODEM_QN 4u
static ax25_frame_t s_q[MODEM_QN];
static volatile uint8_t s_q_head = 0, s_q_tail = 0;
#define MODEM_BADN 3u
static ax25_frame_t s_bad_q[MODEM_BADN];
static volatile uint8_t s_bad_head = 0, s_bad_tail = 0;
static uint16_t s_fix_count = 0;
static uint16_t s_fix2_count = 0;
static uint8_t s_last_fixed = 0;
#define REF_N 4u
#define REF_MAX_LEN 160u
#define REF_MAX_DIFF 4u
typedef struct { uint8_t valid; uint16_t len; uint8_t frame[REF_MAX_LEN]; } ref_t;
static ref_t s_ref[REF_N];
static uint8_t s_ref_pos = 0;
static uint16_t s_rep_count = 0;
static uint8_t s_last_rep = 0;
static ax25_frame_t s_isr_frame;
static ax25_frame_t s_main_frame;

static int16_t s_ring[8];              /* 最近 8 个去直流样本 */
static uint32_t s_nsamp = 0;           /* 累计采样数 */
static int32_t s_dc = 0;               /* 直流（VDD/2 偏置）跟踪 */
static int32_t s_prev_v = 0;           /* 上一次相关差，用于半采样插值 */
static uint16_t s_last_adc = 0;
static uint16_t s_adc_min = 0xFFFFu;
static uint16_t s_adc_max = 0;
static uint8_t s_last_tone = 0;        /* 0=无 1=mark 2=space */
static uint16_t s_mark_hits = 0;
static uint16_t s_space_hits = 0;
static uint16_t s_other_hits = 0;
#define TR_N 9u
typedef struct {
  ax25_hdlc_t hdlc;
  uint32_t next_q8;
  int32_t period_q8;
  int32_t acc;
  uint8_t prev_tone;
  uint8_t have_tone;
  uint8_t have_next;
} tr_dec_t;
static tr_dec_t s_trd[TR_N];
static const int32_t tr_period_q8[3] = { 2022, 2048, 2074 };  /* 7.90 / 8.00 / 8.10 采样/bit */
static const int32_t tr_off_q8[3] = { -128, 0, 128 };          /* ±0.5 采样相位 */
static int32_t s_tr_last_sign = 0;
static int32_t s_tr_cand_sign = 0;
static uint8_t s_tr_cand_cnt = 0;
static uint32_t s_tr_ncross = 0;
static uint8_t s_tr_have_sign = 0;

/* 8 点 × 1200/2200Hz 的 cos/sin 表，缩放 64 倍（9600Hz 采样） */
static const int8_t K_COS1[8] = { 64, 45,   0, -45, -64, -45,   0,  45 };
static const int8_t K_SIN1[8] = {  0, 45,  64,  45,   0, -45, -64, -45 };
static const int8_t K_COS2[8] = { 64,  8, -62, -24,  55,  39, -45, -51 };
static const int8_t K_SIN2[8] = {  0, 63,  17, -59, -32,  51,  45, -39 };


static uint8_t frame_src_info(const uint8_t *frame, uint16_t len, uint8_t *src, const uint8_t **info, uint16_t *ilen)
{
  if (!frame || len < 16u) return 0;
  for (uint8_t i = 0; i < 6u; i++) src[i] = (uint8_t)((frame[7u + i] >> 1) & 0x7Fu);
  src[6] = 0;
  uint16_t idx = 14u;
  while ((frame[idx - 1u] & 0x01u) == 0u) {
    if ((idx + 7u) > len || idx > 70u) return 0;
    idx += 7u;
  }
  if ((idx + 2u) > len) return 0;
  *info = frame + idx + 2u;
  *ilen = (uint16_t)(len - idx - 2u);
  return 1;
}

static void ref_store(const ax25_frame_t *f)
{
  if (!f || f->len > REF_MAX_LEN) return;
  uint8_t src[7];
  const uint8_t *info; uint16_t ilen;
  if (!frame_src_info(f->frame, f->len, src, &info, &ilen)) return;
  for (uint8_t i = 0; i < REF_N; i++) {
    if (!s_ref[i].valid) continue;
    uint8_t rsrc[7]; const uint8_t *rinfo; uint16_t rilen;
    if (!frame_src_info(s_ref[i].frame, s_ref[i].len, rsrc, &rinfo, &rilen)) continue;
    if (memcmp(src, rsrc, 6u) == 0 && ilen == rilen && memcmp(info, rinfo, ilen) == 0) {
      s_ref[i].len = f->len;
      memcpy(s_ref[i].frame, f->frame, f->len);
      return;
    }
  }
  s_ref[s_ref_pos].valid = 1;
  s_ref[s_ref_pos].len = f->len;
  memcpy(s_ref[s_ref_pos].frame, f->frame, f->len);
  s_ref_pos = (uint8_t)((s_ref_pos + 1u) & (REF_N - 1u));
}

static uint8_t ref_match(const ax25_frame_t *f, ax25_frame_t *out)
{
  if (!f || !out || f->len > REF_MAX_LEN) return 0;
  uint8_t src[7]; const uint8_t *info; uint16_t ilen;
  if (!frame_src_info(f->frame, f->len, src, &info, &ilen)) return 0;
  for (uint8_t i = 0; i < REF_N; i++) {
    if (!s_ref[i].valid) continue;
    uint8_t rsrc[7]; const uint8_t *rinfo; uint16_t rilen;
    if (!frame_src_info(s_ref[i].frame, s_ref[i].len, rsrc, &rinfo, &rilen)) continue;
    if (memcmp(src, rsrc, 6u) != 0 || ilen != rilen) continue;
    uint16_t diff = 0;
    for (uint16_t k = 0; k < ilen; k++) {
      uint8_t x = (uint8_t)(info[k] ^ rinfo[k]);
      while (x) { diff++; x &= (uint8_t)(x - 1u); }
      if (diff > REF_MAX_DIFF) break;
    }
    if (diff <= REF_MAX_DIFF) {
      out->len = s_ref[i].len;
      memcpy(out->frame, s_ref[i].frame, s_ref[i].len);
      s_rep_count++;
      s_last_rep = 1;
      s_last_fixed = 0;
      return 1;
    }
  }
  return 0;
}static void modem_push_frame(const ax25_frame_t *f)
{
  uint8_t nh = (uint8_t)((s_q_head + 1u) & (MODEM_QN - 1u));
  if (nh == s_q_tail) return;          /* 队列满：丢弃最新帧 */
  s_q[s_q_head] = *f;
  s_q_head = nh;
}
static void modem_push_bad(const ax25_frame_t *f)
{
  uint8_t nh = (uint8_t)((s_bad_head + 1u) & (MODEM_BADN - 1u));
  if (nh == s_bad_tail) return;          /* 待纠错队列满：丢弃 */
  s_bad_q[s_bad_head] = *f;
  s_bad_head = nh;
}

static void dec_reset(modem_dec_t *d)
{
  d->have = 0;
  d->prev_tone = 0;
  d->acc = 0;
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
  s_prev_v = 0;
  s_last_adc = 0;
  s_adc_min = 0xFFFFu;
  s_adc_max = 0;
  s_last_tone = 0;
  s_q_head = 0; s_q_tail = 0; s_bad_head = 0; s_bad_tail = 0; s_fix_count = 0;
  s_mark_hits = 0;
  s_space_hits = 0;
  s_other_hits = 0;
  for (uint8_t i = 0; i < TR_N; i++) {
    ax25_hdlc_init(&s_trd[i].hdlc);
    s_trd[i].period_q8 = tr_period_q8[i % 3u];
    s_trd[i].acc = 0;
    s_trd[i].next_q8 = 0;
    s_trd[i].prev_tone = 0;
    s_trd[i].have_tone = 0;
    s_trd[i].have_next = 0;
  }
  s_tr_last_sign = 0; s_tr_cand_sign = 0; s_tr_cand_cnt = 0; s_tr_ncross = 0; s_tr_have_sign = 0;
}

static void feed_dec(uint8_t idx)
{
  modem_dec_t *d = &s_dec[idx & (MODEM_NPHASE - 1u)];
  uint8_t tone = (d->acc >= 0) ? 0u : 1u;  /* 0=mark, 1=space */
  d->acc = 0;
  if (!d->have) {
    d->have = 1;
    d->prev_tone = tone;
    return;
  }
  uint8_t bit = (tone == d->prev_tone) ? 1u : 0u;  /* NRZI: 不变=1, 跳变=0 */
  d->prev_tone = tone;

  if (ax25_hdlc_feed_bit(&d->hdlc, bit, &s_isr_frame)) {
    if (ax25_check_frame(s_isr_frame.frame, s_isr_frame.len)) {
      modem_push_frame(&s_isr_frame);
    } else if (ax25_plausible(s_isr_frame.frame, s_isr_frame.len)) {
      modem_push_bad(&s_isr_frame);
    }
  }
}

void modem_adc_sample(uint16_t adc)
{
  s_last_adc = adc;
  if (adc < s_adc_min) s_adc_min = adc;
  if (adc > s_adc_max) s_adc_max = adc;

  /* 一阶直流跟踪：去掉 VDD/2 偏置（1.5V 约 1860 码值） */
  s_dc += ((int32_t)adc - s_dc) >> 4;
  int16_t x = (int16_t)((int32_t)adc - s_dc);

  s_ring[s_nsamp & 7u] = x;
  s_nsamp++;
  if (s_nsamp < 8u) return;

  /* 最近 8 点与 1200/2200Hz 做相关，比较幅度 */
  int32_t i1 = 0, q1 = 0, i2 = 0, q2 = 0;
  for (uint8_t i = 0; i < 8u; i++) {
    int16_t xv = s_ring[(uint16_t)(s_nsamp - 8u + i) & 7u];
    i1 += (int32_t)xv * K_COS1[i];
    q1 += (int32_t)xv * K_SIN1[i];
    i2 += (int32_t)xv * K_COS2[i];
    q2 += (int32_t)xv * K_SIN2[i];
  }
  int32_t m1 = (i1 < 0 ? -i1 : i1) + (q1 < 0 ? -q1 : q1);
  int32_t m2 = (i2 < 0 ? -i2 : i2) + (q2 < 0 ? -q2 : q2);
  if ((m1 + m2) < 500) { s_other_hits++; return; }   /* 只在极弱时才丢弃 */

  int32_t v = m1 - m2;
  int32_t vh = (v + s_prev_v) / 2;                        /* 半采样插值 */
  s_prev_v = v;
  /* 每路相位先积累整段相关差，到采样点再判决：降低单点噪声影响。 */
  for (uint8_t i = 0; i < MODEM_NPHASE; i++)
    s_dec[i].acc += (i & 1u) ? vh : v;
  for (uint8_t i = 0; i < TR_N; i++)
    if (s_trd[i].have_next) s_trd[i].acc += v;

  /* 16 相位：2n 用整采样相位，2n+1 用半采样相位，每个解码器每 8 个采样得到 1 bit */
  uint8_t base = (uint8_t)((2u * (s_nsamp - 1u)) & (MODEM_NPHASE - 1u));
  feed_dec(base);
  feed_dec((uint8_t)(base + 1u));

  /* --- 第 3 条路径：音调跳变重新对齐位时钟（抗 1200 baud/9600Hz 时钟漂移） ---
   * 相关窗中心比实际时间晚约 3.5 采样；检测到跳变后把采样点定在
   * n_cross+4（T/2），之后按 8 采样/bit 自由运行，遇到下一次跳变再对齐。 */
  int tr_sign = (v > 0) ? 1 : ((v < 0) ? -1 : 0);
  if (tr_sign != 0) {
    if (tr_sign == s_tr_cand_sign) s_tr_cand_cnt++;
    else { s_tr_cand_sign = tr_sign; s_tr_cand_cnt = 1; s_tr_ncross = s_nsamp - 1u; }
  }
  if (s_tr_cand_sign != 0 && s_tr_cand_cnt >= 2u &&
      (!s_tr_have_sign || s_tr_cand_sign != s_tr_last_sign)) {
    s_tr_last_sign = s_tr_cand_sign;
    s_tr_have_sign = 1;
    uint32_t base_q8 = s_tr_ncross * 256u;
    for (uint8_t i = 0; i < TR_N; i++) {
      s_trd[i].next_q8 = base_q8 + (uint32_t)(s_trd[i].period_q8 / 2) + (uint32_t)tr_off_q8[i / 3u];
      s_trd[i].have_next = 1;
    }
  }
  {
    uint32_t cur_q8 = s_nsamp * 256u;
    for (uint8_t i = 0; i < TR_N; i++) {
      tr_dec_t *d = &s_trd[i];
      if (!d->have_next) continue;
      int32_t diff = (int32_t)(cur_q8 - d->next_q8);
      if (diff < 0) continue;
      if (diff > (d->period_q8 * 8)) {   /* 落后太多：重新对准 */
        d->next_q8 = cur_q8 + (uint32_t)(d->period_q8 / 2);
        continue;
      }
      uint8_t tone = (d->acc >= 0) ? 0u : 1u;
      d->acc = 0;
      if (d->have_tone) {
        uint8_t bit = (tone == d->prev_tone) ? 1u : 0u;
        if (ax25_hdlc_feed_bit(&d->hdlc, bit, &s_isr_frame)) {
          if (ax25_check_frame(s_isr_frame.frame, s_isr_frame.len)) {
            modem_push_frame(&s_isr_frame);
          } else if (ax25_plausible(s_isr_frame.frame, s_isr_frame.len)) {
            modem_push_bad(&s_isr_frame);
          }
        }
      }
      d->prev_tone = tone;
      d->have_tone = 1;
      d->next_q8 += (uint32_t)d->period_q8;
    }
  }  s_last_tone = (uint8_t)((v >= 0 ? 0u : 1u) + 1u);
  if (v >= 0) s_mark_hits++; else s_space_hits++;
}

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

uint16_t modem_get_fix_count(void)
{
  return s_fix_count;
}

uint8_t modem_frame_was_fixed(void)
{
  return s_last_fixed;
}

uint16_t modem_get_fix2_count(void)
{
  return s_fix2_count;
}

uint8_t modem_frame_was_repeat(void)
{
  return s_last_rep;
}

uint16_t modem_get_rep_count(void)
{
  return s_rep_count;
}

uint8_t modem_get_frame(ax25_frame_t *out)
{
  if (!out) return 0;
  if (s_q_tail != s_q_head) {
    *out = s_q[s_q_tail];
    s_q_tail = (uint8_t)((s_q_tail + 1u) & (MODEM_QN - 1u));
    s_last_fixed = 0;
    s_last_rep = 0;
    ref_store(out);
    return 1;
  }
  while (s_bad_tail != s_bad_head) {
    s_main_frame = s_bad_q[s_bad_tail];
    s_bad_tail = (uint8_t)((s_bad_tail + 1u) & (MODEM_BADN - 1u));
    if (ax25_correct_single_bit(s_main_frame.frame, s_main_frame.len)) {
      s_fix_count++;
      s_last_fixed = 1;
      s_last_rep = 0;
      *out = s_main_frame;
      return 1;
    }
    if (ax25_correct_two_bits(s_main_frame.frame, s_main_frame.len)) {
      s_fix2_count++;
      s_fix_count++;
      s_last_fixed = 1;
      s_last_rep = 0;
      *out = s_main_frame;
      return 1;
    }
    if (ref_match(&s_main_frame, out)) {
      return 1;
    }
  }
  return 0;
}
