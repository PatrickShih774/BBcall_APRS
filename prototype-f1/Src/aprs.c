/*
 * APRS 消息解析：':' + 9字符地址 + ':' + 正文[ + '|' + 消息ID ]
 */
#include "aprs.h"
#include <string.h>

uint8_t aprs_parse_message(const uint8_t *info, uint16_t len, aprs_message_t *m)
{
    if (len < 11 || info[0] != ':') return 0;
    memset(m, 0, sizeof(*m));

    /* addressee: info[1..9] */
    uint16_t a_len = 0;
    for (uint16_t i = 1; i < 10; i++) {
        uint8_t c = info[i];
        if (c == ' ') continue;            /* 跳过填充空格，保留中间字符 */
        if (a_len < APRS_ADDR_MAX - 1) m->addressee[a_len++] = (char)c;
    }
    m->addressee[a_len] = '\0';
    m->addressee_len = a_len;

    /* body: info[11..] */
    uint16_t b_len = 0;
    for (uint16_t i = 11; i < len; i++) {
        uint8_t c = info[i];
        if (c == '|') {                     /* 消息 ID 分隔符 */
            m->has_msg_id = 1;
            for (uint16_t k = 0; k < 7 && i + 1 + k < len; k++) m->msg_id[k] = info[i + 1 + k];
            break;
        }
        if (b_len < APRS_BODY_MAX - 1) m->body[b_len++] = c;
    }
    m->body[b_len] = 0;
    m->body_len = b_len;
    return 1;
}
