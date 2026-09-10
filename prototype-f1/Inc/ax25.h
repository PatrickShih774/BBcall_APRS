#ifndef AX25_H
#define AX25_H

#include <stdint.h>

#define AX25_MAX_FRAME   330

typedef struct {
    uint8_t  frame[AX25_MAX_FRAME];
    uint16_t len;                /* 含 FCS */
} ax25_frame_t;

/* 流式 HDLC：从数据比特流恢复帧 */
typedef struct {
    uint16_t shift;              /* flag 检测移位寄存器 */
    uint8_t  ones;               /* 连续 1 计数（去填充） */
    uint8_t  byte;
    uint8_t  nbits;
    uint8_t  frame[AX25_MAX_FRAME];
    uint16_t fpos;
    uint8_t  in_frame;
} ax25_hdlc_t;

void ax25_hdlc_init(ax25_hdlc_t *h);
/* 喂一个数据比特（NRZI 已解码的 0/1）；返回 1 表示送出一帧到 out */
uint8_t ax25_hdlc_feed_bit(ax25_hdlc_t *h, uint8_t bit, ax25_frame_t *out);

/* 一次性解码：给字节流（已去 flag 的帧体）, 返回 0=CRC 坏, 1=好 */
uint8_t ax25_check_frame(const uint8_t *body, uint16_t len);

/* 解析帧：把解析出的字段写入 out */
typedef struct {
    char     dest[7];
    uint8_t  dest_ssid;
    char     src[7];
    uint8_t  src_ssid;
    uint8_t  path[4][7];
    uint8_t  path_ssid[4];
    uint8_t  npath;
    uint8_t  control;
    uint8_t  pid;
    uint16_t info_len;
    uint8_t  info[AX25_MAX_FRAME];
} ax25_decoded_t;

/* body 为不含 flag、含最后 2 字节 FCS 的完整帧 */
uint8_t ax25_decode(const uint8_t *body, uint16_t len, ax25_decoded_t *out);

/* CRC-16/X.25 工具 */
uint16_t ax25_fcs(const uint8_t *data, uint16_t len);

#endif /* AX25_H */
