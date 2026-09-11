/*
 * 模拟器数据源：把真实 APRS 数据送进 UI harness
 *
 *   sim_feed_wav()   WAV -> 9600Hz ADC 采样 -> 固件 modem.c 解调 -> AX.25 -> UI
 *   sim_feed_log()   串口日志里的 [RAW] len=.. hex=.. -> AX.25 -> UI
 *   sim_feed_demo()  内置示例帧（无外部文件时的自检数据）
 *
 * WAV 路径完全复用固件解调代码：modem.c 的入口就是 modem_adc_sample(adc)，
 * 这里只要把音频重采样成 9600Hz 并映射到 12bit ADC 量程即可。
 */
#include "sim_feed.h"
#include "ui_harness.h"
#include "modem.h"
#include "ax25.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 与真机 ADC 一致的映射：中心 2048，幅度 ±800（约 0.65Vpp @3.3V 参考） */
#define SIM_ADC_CENTER 2048
#define SIM_ADC_AMP    800
#define SIM_FS_HZ      9600u

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t adc_from_s16(int32_t v)
{
  int32_t a = SIM_ADC_CENTER + v * SIM_ADC_AMP / 32768;
  if (a < 0) a = 0;
  if (a > 4095) a = 4095;
  return a;
}

static int drain_frames(uint32_t t_ms, int *count)
{
  ax25_frame_t fr;
  while (modem_get_frame(&fr)) {
    uint8_t fixed = modem_frame_was_fixed();
    uint8_t rep   = modem_frame_was_repeat();
    if (ui_feed_ax25(fr.frame, fr.len, t_ms, fixed, rep)) (*count)++;
  }
  return *count;
}

int sim_feed_wav(const char *path)
{
  FILE *f;
  long sz;
  uint8_t *buf;
  uint16_t fmt = 0, ch = 0, bits = 0;
  uint32_t rate = 0, p;
  const uint8_t *data = NULL;
  uint32_t dlen = 0, frames;
  int count = 0;

  if (!path) return -1;
  f = fopen(path, "rb");
  if (!f) { printf("[sim] 打不开 WAV: %s\n", path); return -1; }
  fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
  if (sz < 44) { fclose(f); return -2; }
  buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) { fclose(f); return -3; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -4; }
  fclose(f);
  if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) { free(buf); return -5; }

  p = 12;
  while (p + 8u <= (uint32_t)sz) {
    uint32_t csz = rd32(buf + p + 4);
    const uint8_t *ck = buf + p + 8;
    if (memcmp(buf + p, "fmt ", 4) == 0 && csz >= 16u) {
      fmt = rd16(ck); ch = rd16(ck + 2); rate = rd32(ck + 4); bits = rd16(ck + 14);
    } else if (memcmp(buf + p, "data", 4) == 0) {
      data = ck; dlen = csz;
    }
    p += 8u + csz + (csz & 1u);
  }
  if (!data || fmt != 1u || bits != 16u || ch < 1u || rate == 0u) {
    printf("[sim] WAV 需要 16bit PCM（fmt=%u bits=%u ch=%u rate=%u）\n",
           (unsigned)fmt, (unsigned)bits, (unsigned)ch, (unsigned)rate);
    free(buf);
    return -6;
  }
  frames = dlen / (2u * ch);
  if (data + (size_t)dlen > buf + (size_t)sz) { free(buf); return -7; }

  printf("[sim] WAV %s: %u Hz / %u ch / %u 帧 (%.3f s)\n",
         path, (unsigned)rate, (unsigned)ch, (unsigned)frames,
         (double)frames / (double)rate);

  modem_init();

  if (rate % SIM_FS_HZ == 0u && rate >= SIM_FS_HZ) {
    /* 整数抽取：先用 (rate/9600) 点滑动平均抗混叠，再降到 9600Hz */
    uint32_t dec = rate / SIM_FS_HZ;
    uint32_t i, k;
    for (i = 0; i + dec <= frames; i += dec) {
      int32_t sum = 0;
      for (k = 0; k < dec; k++) {
        const uint8_t *sp = data + (size_t)(i + k) * 2u * ch;
        int32_t v = (int32_t)(int16_t)rd16(sp);
        uint16_t c;
        for (c = 1u; c < ch; c++) v += (int32_t)(int16_t)rd16(sp + 2u * c);
        sum += v / (int32_t)ch;
      }
      modem_adc_sample((uint16_t)adc_from_s16(sum / (int32_t)dec));
      drain_frames((uint32_t)((uint64_t)(i / dec) * 1000u / SIM_FS_HZ), &count);
    }
  } else {
    /* 一般情况：线性插值重采样 */
    uint32_t i, n_out = (uint32_t)((uint64_t)frames * SIM_FS_HZ / rate);
    for (i = 0; i < n_out; i++) {
      double src = (double)i * (double)rate / (double)SIM_FS_HZ;
      uint32_t i0 = (uint32_t)src;
      double frac = src - (double)i0;
      int32_t v0, v1, v;
      if (i0 + 1u >= frames) i0 = (frames > 0u) ? (frames - 1u) : 0u;
      v0 = (int32_t)(int16_t)rd16(data + (size_t)i0 * 2u * ch);
      v1 = (int32_t)(int16_t)rd16(data + (size_t)((i0 + 1u < frames) ? (i0 + 1u) : i0) * 2u * ch);
      v = v0 + (int32_t)((double)(v1 - v0) * frac);
      modem_adc_sample((uint16_t)adc_from_s16(v));
      drain_frames((uint32_t)((uint64_t)i * 1000u / SIM_FS_HZ), &count);
    }
  }

  free(buf);

  {
    uint16_t mk = 0, sp = 0, ot = 0;
    modem_get_stats(&mk, &sp, &ot);
    printf("[sim] 解调统计: mark=%u space=%u other=%u  FIX=%u FIX2=%u REP=%u  -> 收帧 %d\n",
           (unsigned)mk, (unsigned)sp, (unsigned)ot,
           (unsigned)modem_get_fix_count(), (unsigned)modem_get_fix2_count(),
           (unsigned)modem_get_rep_count(), count);
  }
  return count;
}

/* ---------------- 串口日志回放 ---------------- */
static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static const char *find_sub(const char *hay, const char *needle)
{
  size_t nl = strlen(needle);
  while (*hay) {
    if (strncmp(hay, needle, nl) == 0) return hay;
    hay++;
  }
  return NULL;
}

int sim_feed_log(const char *path)
{
  FILE *f;
  long sz;
  char *buf;
  char *line;
  int count = 0, seen = 0;

  if (!path) return -1;
  f = fopen(path, "rb");
  if (!f) { printf("[sim] 打不开日志: %s\n", path); return -1; }
  fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return -2; }
  buf = (char *)malloc((size_t)sz + 1u);
  if (!buf) { fclose(f); return -3; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -4; }
  fclose(f);
  buf[sz] = 0;

  line = buf;
  while (*line) {
    char *eol = line;
    while (*eol && *eol != '\n' && *eol != '\r') eol++;
    if (*eol) { *eol = 0; eol++; }
    {
      const char *hx = find_sub(line, "hex=");
      if (hx) {
        uint8_t frame[300];
        uint16_t n = 0;
        const char *q = hx + 4;
        uint32_t t_ms = 0;
        uint8_t fixed = 0, rep = 0;
        const char *ts = find_sub(line, "[T=");
        while (n < (uint16_t)sizeof(frame)) {
          int hi = hexval((unsigned char)q[0]);
          int lo = hexval((unsigned char)q[1]);
          if (hi < 0 || lo < 0) break;
          frame[n++] = (uint8_t)((hi << 4) | lo);
          q += 2;
        }
        if (find_sub(line, "[FIX]")) fixed = 1;
        if (find_sub(line, "[REP]")) rep = 1;
        if (ts) {
          const char *d0 = ts + 3;
          while (*d0 >= '0' && *d0 <= '9') { t_ms = t_ms * 10u + (uint32_t)(*d0 - '0'); d0++; }
        }
        seen++;
        if (n < 16u) continue;
        if (!ax25_check_frame(frame, n)) {
          if (!ax25_correct_single_bit(frame, n) && !ax25_correct_two_bits(frame, n)) {
            printf("[sim] 第 %d 帧 CRC 失败，跳过\n", seen);
            continue;
          }
          fixed = 1;
        }
        if (ui_feed_ax25(frame, n, t_ms, fixed, rep)) count++;
      }
    }
    line = eol;
  }
  free(buf);
  printf("[sim] 日志 %s: 发现 %d 个 [RAW] 帧，注入 %d 条\n", path, seen, count);
  return count;
}

/* ---------------- 内置示例 ---------------- */
/* 与 tools/ax25_reference.py 的 build_frame 等价，避免依赖外部文件 */
static uint8_t *demo_frame(const char *dest, const char *src, const char *info, uint16_t *out_len)
{
  static uint8_t frame[300];
  static uint8_t body[300];
  uint16_t n = 0, i, blen = 0;
  uint16_t crc = 0xFFFFu;
  const char *p;
  const char *calls[2];

  calls[0] = dest; calls[1] = src;
  for (i = 0; i < 2u; i++) {
    char call[7];
    uint8_t k;
    for (k = 0; k < 6u; k++) call[k] = ' ';
    call[6] = 0;
    for (k = 0; k < 6u && calls[i][k]; k++) call[k] = calls[i][k];
    for (k = 0; k < 6u; k++) body[blen++] = (uint8_t)(((uint8_t)call[k] << 1) & 0xFEu);
    body[blen++] = (uint8_t)(0x60u | 0x00u);
  }
  body[blen - 1] |= 0x01u;   /* 最后一个地址字节：扩展位 */
  body[blen++] = 0x03u;      /* UI */
  body[blen++] = 0xF0u;      /* PID */
  for (p = info; *p; p++) body[blen++] = (uint8_t)*p;

  for (i = 0; i < blen; i++) {
    uint8_t b = body[i];
    uint8_t j;
    crc ^= b;
    for (j = 0; j < 8u; j++) {
      if (crc & 1u) crc = (uint16_t)((crc >> 1) ^ 0x8408u);
      else          crc >>= 1;
    }
  }
  crc = (uint16_t)(~crc);
  for (i = 0; i < blen; i++) frame[n++] = body[i];
  frame[n++] = (uint8_t)(crc & 0xFFu);
  frame[n++] = (uint8_t)((crc >> 8) & 0xFFu);
  *out_len = n;
  return frame;
}

int sim_feed_demo(void)
{
  static const struct { const char *dst; const char *src; const char *info; } demo[4] = {
    { "APRS    ", "BG5BLH", ":BG5BLH   :Hello APRS 144.640" },
    { "APRS    ", "BG5AOZ", "=2945.58N/12137.26E[BBcall from MMradio/A=000039" },
    { "APRS    ", "BY4SZ ",  ">Status: BBcall_APRS simulator  144.640MHz" },
    { "APRS    ", "BG5BLH", "!2954.05N/12132.86E>Test position 144.640MHz" }
  };
  int i, count = 0;
  for (i = 0; i < 4; i++) {
    uint16_t len = 0;
    uint8_t *fr = demo_frame(demo[i].dst, demo[i].src, demo[i].info, &len);
    if (ax25_check_frame(fr, len) && ui_feed_ax25(fr, len, (uint32_t)(1000 + i * 1500), 0, 0)) count++;
  }
  printf("[sim] 内置示例注入 %d 条\n", count);
  return count;
}