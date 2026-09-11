/* AX.25 (HDLC) 解码：CRC-16/X.25 + 地址/控制/PID/信息 解析（主机已验证）。 */
#include "ax25.h"
#include <string.h>

static uint16_t crc_x25(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001u) crc = (uint16_t)((crc >> 1) ^ 0x8408u);
      else               crc >>= 1;
    }
  }
  return crc;
}

uint8_t ax25_check_frame(const uint8_t *body, uint16_t len)
{
  if (len < 16u) return 0;
  return crc_x25(body, len) == 0xF0B8u;
}

void ax25_hdlc_init(ax25_hdlc_t *h)
{
  h->shift = 0; h->ones = 0; h->byte = 0; h->nbits = 0;
  h->fpos = 0; h->in_frame = 0;
}

uint8_t ax25_hdlc_feed_bit(ax25_hdlc_t *h, uint8_t bit, ax25_frame_t *out)
{
  /* AX.25/HDLC 按 LSB-first 发送：移位寄存器右移，新比特进 MSB。
   * 检测到 0x7E 标志后必须清零移位寄存器，否则后续字节会错位。 */
  h->shift = (uint16_t)(((h->shift >> 1) | (bit ? 0x80u : 0u)) & 0xFFu);
  if (h->shift == 0x7Eu) {
    if (h->in_frame && h->fpos >= 15u && out && h->fpos <= AX25_MAX_FRAME) {
      memcpy(out->frame, h->frame, h->fpos);
      out->len = h->fpos;
      h->in_frame = 0; h->fpos = 0; h->nbits = 0; h->ones = 0; h->byte = 0; h->shift = 0;
      return 1;
    }
    h->in_frame = 1; h->fpos = 0; h->nbits = 0; h->ones = 0; h->byte = 0; h->shift = 0;
    return 0;
  }
  if (h->in_frame) {
    if (h->ones == 5u && bit == 0u) { h->ones = 0; return 0; }
    if (bit) { h->ones++; if (h->ones > 6u) { h->in_frame = 0; return 0; } }
    else     { h->ones = 0; }
    h->byte = (uint8_t)((h->byte >> 1) | (bit ? 0x80u : 0u));
    h->nbits++;
    if (h->nbits == 8u) {
      if (h->fpos < AX25_MAX_FRAME) h->frame[h->fpos++] = h->byte;
      h->byte = 0; h->nbits = 0;
    }
  } else {
    if (bit) h->ones++; else h->ones = 0;
  }
  return 0;
}

static void decode_call(const uint8_t *a7, char *call, uint8_t *ssid)
{
  for (int i = 0; i < 6; i++) {
    uint8_t c = (uint8_t)((a7[i] >> 1) & 0x7Fu);
    if (c == 0x20u) break;
    call[i] = (char)c;
  }
  call[6] = '\0';
  *ssid = (uint8_t)((a7[6] >> 1) & 0x0Fu);
}


/* ---------- 2-bit 纠错（CRC 校正子表） ---------- */
#define AX25_SYN_MAX_BITS (160u * 8u)
static uint16_t s_syn[AX25_SYN_MAX_BITS];

static uint16_t crc_step_zero(uint16_t c)
{
  return (uint16_t)((c & 1u) ? ((c >> 1) ^ 0x8408u) : (c >> 1));
}

uint8_t ax25_correct_two_bits(uint8_t *frame, uint16_t len)
{
  if (!frame || len < 15u || len > 160u) return 0;
  uint16_t nbits = (uint16_t)(len * 8u);
  uint16_t s = 0x8408u;
  for (int k = (int)nbits - 1; k >= 0; k--) {
    s_syn[k] = s;
    s = crc_step_zero(s);
  }
  uint16_t target = (uint16_t)(crc_x25(frame, len) ^ 0xF0B8u);
  for (uint16_t i = 0; i < nbits; i++) {
    uint16_t si = s_syn[i];
    for (uint16_t j = (uint16_t)(i + 1u); j < nbits; j++) {
      if ((uint16_t)(si ^ s_syn[j]) == target) {
        frame[i >> 3] ^= (uint8_t)(1u << (i & 7u));
        frame[j >> 3] ^= (uint8_t)(1u << (j & 7u));
        if (ax25_check_frame(frame, len)) return 1;
        frame[i >> 3] ^= (uint8_t)(1u << (i & 7u));
        frame[j >> 3] ^= (uint8_t)(1u << (j & 7u));
      }
    }
  }
  return 0;
}
uint8_t ax25_plausible(const uint8_t *frame, uint16_t len)
{
  if (!frame || len < 18u || len > 150u) return 0;
  uint8_t alnum = 0;
  for (uint8_t a = 0; a < 2u; a++) {
    for (uint8_t i = 0; i < 6u; i++) {
      uint8_t c = (uint8_t)((frame[a * 7u + i] >> 1) & 0x7Fu);
      if (c == 0x20u) continue;
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) return 0;
      alnum = 1;
    }
  }
  if (!alnum) return 0;
  uint16_t idx = 14u;
  while ((frame[idx - 1u] & 0x01u) == 0u) {
    if ((idx + 7u) > len || idx > 70u) return 0;
    idx += 7u;
  }
  if ((idx + 2u) > len) return 0;
  if (frame[idx] != 0x03u) return 0;       /* AX.25 UI 帧 */
  if (frame[idx + 1u] != 0xF0u) return 0;  /* APRS PID */
  return 1;
}

uint8_t ax25_correct_single_bit(uint8_t *frame, uint16_t len)
{
  if (!frame || len < 15u || len > 160u) return 0;   /* 限制运算量，典型 APRS 帧 60~120 字节 */
  for (uint16_t i = 0; i < len; i++) {
    for (uint8_t b = 0; b < 8u; b++) {
      frame[i] ^= (uint8_t)(1u << b);
      if (ax25_check_frame(frame, len)) return 1;
      frame[i] ^= (uint8_t)(1u << b);
    }
  }
  return 0;
}uint8_t ax25_decode(const uint8_t *body, uint16_t len, ax25_decoded_t *out)
{
  if (!ax25_check_frame(body, len)) return 0;
  memset(out, 0, sizeof(*out));
  decode_call(&body[0], out->dest, &out->dest_ssid);
  uint16_t idx = 7;
  decode_call(&body[idx], out->src, &out->src_ssid);
  idx += 7;
  while ((body[idx - 1] & 0x01u) == 0u && out->npath < 4u && (idx + 6u) < len) {
    out->path_h[out->npath] = (uint8_t)((body[idx + 6u] & 0x80u) ? 1u : 0u);
    decode_call(&body[idx], (char *)out->path[out->npath], &out->path_ssid[out->npath]);
    idx += 7;
    out->npath++;
  }
  out->control = body[idx];
  out->pid = body[idx + 1];
  uint16_t info_len = len - 2u - (idx + 2u);
  if (info_len > AX25_MAX_FRAME) info_len = AX25_MAX_FRAME;
  out->info_len = info_len;
  memcpy(out->info, &body[idx + 2u], info_len);
  return 1;
}
