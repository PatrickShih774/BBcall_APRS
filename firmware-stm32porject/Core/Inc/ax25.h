#ifndef AX25_H
#define AX25_H
#include <stdint.h>
#define AX25_MAX_FRAME 330
typedef struct { uint8_t frame[AX25_MAX_FRAME]; uint16_t len; } ax25_frame_t;
typedef struct {
    uint16_t shift;
    uint8_t ones;
    uint8_t byte;
    uint8_t nbits;
    uint8_t frame[AX25_MAX_FRAME];
    uint16_t fpos;
    uint8_t in_frame;
} ax25_hdlc_t;
void ax25_hdlc_init(ax25_hdlc_t *h);
uint8_t ax25_hdlc_feed_bit(ax25_hdlc_t *h, uint8_t bit, ax25_frame_t *out);
typedef struct {
    char dest[7];
    uint8_t dest_ssid;
    char src[7];
    uint8_t src_ssid;
    uint8_t path[4][7];
    uint8_t path_ssid[4];
    uint8_t npath;
    uint8_t control;
    uint8_t pid;
    uint16_t info_len;
    uint8_t info[AX25_MAX_FRAME];
} ax25_decoded_t;
uint8_t ax25_check_frame(const uint8_t *body, uint16_t len);
uint8_t ax25_decode(const uint8_t *body, uint16_t len, ax25_decoded_t *out);
#endif
