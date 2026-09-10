#ifndef APRS_H
#define APRS_H

#include <stdint.h>

#define APRS_ADDR_MAX 10
#define APRS_BODY_MAX 72

typedef struct {
    char     addressee[APRS_ADDR_MAX];   /* 9 字符，通常为呼号 */
    uint8_t  addressee_len;
    uint8_t  body[APRS_BODY_MAX];        /* 原始正文（UTF-8 字节） */
    uint8_t  body_len;
    uint8_t  has_msg_id;
    uint8_t  msg_id[8];
} aprs_message_t;

/* 解析 APRS 消息：info 为 AX.25 信息字段；返回 1 表示是消息 */
uint8_t aprs_parse_message(const uint8_t *info, uint16_t len, aprs_message_t *m);

#endif /* APRS_H */
