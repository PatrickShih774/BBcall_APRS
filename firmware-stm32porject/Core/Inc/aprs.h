#ifndef APRS_H
#define APRS_H
#include <stdint.h>
#define APRS_ADDR_MAX 10
#define APRS_BODY_MAX 72
typedef struct {
    char addressee[APRS_ADDR_MAX];
    uint8_t addressee_len;
    uint8_t body[APRS_BODY_MAX];
    uint8_t body_len;
    uint8_t has_msg_id;
    uint8_t msg_id[8];
} aprs_message_t;
uint8_t aprs_parse_message(const uint8_t *info, uint16_t len, aprs_message_t *m);
#endif
