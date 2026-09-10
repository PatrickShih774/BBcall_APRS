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

/* Mic-E（APRS 位置格式，信息域以 ` 或 ' 开头）解码结果 */
typedef struct {
    uint8_t valid;
    char lat[10];        /* 例如 2948.07N */
    char lon[11];        /* 例如 12138.24E */
    char mtype[24];      /* M0: Off Duty 等 */
    uint16_t speed_kmh;
    uint16_t course;
    char comment[APRS_BODY_MAX];
    uint8_t symbol;
    char symbol_table;
} aprs_mice_t;

uint8_t aprs_parse_message(const uint8_t *info, uint16_t len, aprs_message_t *m);
uint8_t aprs_parse_mice(const char *dest, const uint8_t *info, uint16_t len, aprs_mice_t *m);
#endif