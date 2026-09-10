/*
 * AX.25 (HDLC) 解码：CRC-16/X.25 + 地址/控制/PID/信息 解析。
 * 与 tools/ax25_reference.py 保持一致。
 */
#include "ax25.h"
#include <string.h>

/* 原始 CRC（无最终取反），多项式 0x8408（反射 0x1021） */
static uint16_t crc_x25(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (uint16_t)((crc >> 1) ^ 0x8408);
            else              crc >>= 1;
        }
    }
    return crc;
}

uint16_t ax25_fcs(const uint8_t *data, uint16_t len)
{
    return (uint16_t)(~crc_x25(data, len));
}

uint8_t ax25_check_frame(const uint8_t *body, uint16_t len)
{
    if (len < 16) return 0;
    return crc_x25(body, len) == 0xF0B8u;   /* 标准 AX.25 残差 */
}

void ax25_hdlc_init(ax25_hdlc_t *h)
{
    h->shift = 0; h->ones = 0; h->byte = 0; h->nbits = 0;
    h->fpos = 0; h->in_frame = 0;
}

#define FLAG 0x7Eu
#define AX25_MIN_FRAME 15u

uint8_t ax25_hdlc_feed_bit(ax25_hdlc_t *h, uint8_t bit, ax25_frame_t *out)
{
    h->shift = (uint16_t)((h->shift << 1) | (bit & 1));

    if (h->shift == FLAG) {
        if (h->in_frame && h->fpos >= AX25_MIN_FRAME) {
            if (out && h->fpos <= AX25_MAX_FRAME) {
                memcpy(out->frame, h->frame, h->fpos);
                out->len = h->fpos;
                h->in_frame = 0; h->fpos = 0; h->nbits = 0; h->ones = 0; h->byte = 0;
                return 1;
            }
        }
        /* 首 flag 或空帧：重置进入接收态 */
        h->in_frame = 1; h->fpos = 0; h->nbits = 0; h->ones = 0; h->byte = 0;
        return 0;
    }

    if (h->in_frame) {
        /* 去填充：5 个 1 后出现 0 -> 丢弃该 0 */
        if (h->ones == 5 && bit == 0) { h->ones = 0; return 0; }
        if (bit) { h->ones++; if (h->ones > 6) { h->in_frame = 0; return 0; } }
        else     { h->ones = 0; }

        h->byte = (uint8_t)((h->byte << 1) | bit);
        h->nbits++;
        if (h->nbits == 8) {
            if (h->fpos < AX25_MAX_FRAME) h->frame[h->fpos++] = h->byte;
            h->byte = 0; h->nbits = 0;
        }
    } else {
        if (bit) h->ones++; else h->ones = 0;
    }
    return 0;
}

static void decode_call(const uint8_t *addr7, char *call, uint8_t *ssid)
{
    for (int i = 0; i < 6; i++) {
        uint8_t c = (uint8_t)((addr7[i] >> 1) & 0x7F);
        if (c == 0x20) break;                 /* 空格填充终止 */
        call[i] = (char)c;
    }
    call[6] = '\0';
    *ssid = (uint8_t)((addr7[6] >> 1) & 0x0F);
}

uint8_t ax25_decode(const uint8_t *body, uint16_t len, ax25_decoded_t *out)
{
    if (!ax25_check_frame(body, len)) return 0;
    memset(out, 0, sizeof(*out));

    decode_call(&body[0], out->dest, &out->dest_ssid);
    uint16_t idx = 7;
    decode_call(&body[idx], out->src, &out->src_ssid);
    idx += 7;

    out->npath = 0;
    while ((body[idx - 1] & 0x01) == 0 && out->npath < 4 && (idx + 6) < len) {
        decode_call(&body[idx], (char *)out->path[out->npath], &out->path_ssid[out->npath]);
        idx += 7;
        out->npath++;
    }

    out->control = body[idx];
    out->pid     = body[idx + 1];
    uint16_t info_len = len - 2 - (idx + 2);
    if (info_len > AX25_MAX_FRAME) info_len = AX25_MAX_FRAME;
    out->info_len = info_len;
    memcpy(out->info, &body[idx + 2], info_len);
    return 1;
}
