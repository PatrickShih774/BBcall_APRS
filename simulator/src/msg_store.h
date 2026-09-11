#ifndef MSG_STORE_H
#define MSG_STORE_H
#include <stdint.h>

/* 参数与 GOGUFW（Gogu-Qs/GOGUFW-UV-K1-Messenger, Apache-2.0）对齐，
 * 便于两端行为可比；正文上限 36 字符是那套 UI 的实测上限。 */
#define MSG_TEXT_MAX     36
#define MSG_CALL_MAX     8
#define MSG_INBOX_MAX    16
#define MSG_OUTBOX_MAX   8
#define MSG_DRAFT_MAX    8
#define MSG_ACK_SRC_MAX  3
#define MSG_ACK_ID_MAX   8

#define MSG_ST_NONE      0u
#define MSG_ST_PENDING   1u
#define MSG_ST_ACKED     2u
#define MSG_ST_FAILED    3u

typedef struct {
  uint8_t used;
  uint8_t unread;
  uint16_t id;
  uint16_t age_s;
  char    from[MSG_CALL_MAX + 1];
  char    text[MSG_TEXT_MAX + 1];
} msg_in_t;

typedef struct {
  uint8_t used;
  uint8_t status;
  uint16_t id;
  uint16_t age_s;
  char    to[MSG_CALL_MAX + 1];
  char    text[MSG_TEXT_MAX + 1];
  uint8_t ack_count;
  char    ack_from[MSG_ACK_SRC_MAX][MSG_ACK_ID_MAX + 1];
} msg_out_t;

void     msg_store_init(void);
void     msg_store_tick(uint32_t ms);          /* 推进所有条目的 age_s */

/* 收到一条 APRS 消息正文。返回 1=入箱，0=被当作 ACK 或重复包吞掉 */
uint8_t  msg_store_add_inbox(const char *from, const char *text, uint16_t id);
/* 收到一个带 msg_id 的 APRS 消息时用这个（id 来自发送方） */
uint8_t  msg_store_add_inbox_id(const char *from, const char *text, uint16_t id);

void     msg_store_add_outbox(const char *to, const char *text, uint16_t id);
uint8_t  msg_store_ack(uint16_t id, const char *from);   /* 收到 ackNNN */

uint8_t  msg_store_count_inbox(void);
uint8_t  msg_store_count_outbox(void);
uint8_t  msg_store_count_drafts(void);
uint8_t  msg_store_unread(void);
uint8_t  msg_store_has_unread(void);
uint16_t msg_store_total_rx(void);
uint16_t msg_store_total_ack(void);

msg_in_t  *msg_store_inbox(uint8_t i);
msg_out_t *msg_store_outbox(uint8_t i);
void       msg_store_mark_read(uint8_t i);
void       msg_store_delete_inbox(uint8_t i);
void       msg_store_delete_outbox(uint8_t i);

/* 草稿（定长槽位，空槽 text[0]==0） */
const char *msg_store_draft(uint8_t i);
void        msg_store_set_draft(uint8_t i, const char *text);

/* "NOW" / "12m" / "3h" */
void     msg_store_fmt_age(uint16_t age_s, char *buf, uint8_t cap);
/* 演示数据：与 GOGUFW 一样提供 demo 注入，便于无发射场景下验证 UI */
void     msg_store_add_demo(void);
#endif