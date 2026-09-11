/*
 * Messenger 数据模型（收件箱 + Sent/ACK 数据结构；草稿已随组包屏一起移除）
 *
 * 字段与容量参考 GOGUFW（Gogu-Qs/GOGUFW-UV-K1-Messenger，Apache-2.0）：
 * 正文 36 字符、Inbox 16 / Sent 8、按 (from, id) 去重、多源 ACK。
 * 列表顺序按本项目约定：最新在上。
 */
#include "msg_store.h"
#include <string.h>
#include <stdio.h>

static msg_in_t  s_in[MSG_INBOX_MAX];
static msg_out_t s_out[MSG_OUTBOX_MAX];
static uint16_t  s_next_id = 1u;
static uint16_t  s_total_rx;
static uint16_t  s_total_ack;

static void clip(char *dst, uint8_t cap, const char *src)
{
  uint8_t i = 0;
  if (cap == 0u) return;
  while ((uint8_t)(i + 1u) < cap && src && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

/* APRS 消息里的非可打印字节统一替换成 '.'，避免把控制字符画上屏 */
static void clip_printable(char *dst, uint8_t cap, const char *src, uint8_t len)
{
  uint8_t i, n = 0;
  if (cap == 0u) return;
  for (i = 0; i < len && (uint8_t)(n + 1u) < cap; i++) {
    uint8_t c = (uint8_t)src[i];
    dst[n++] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '.';
  }
  dst[n] = 0;
}

void msg_store_init(void)
{
  memset(s_in, 0, sizeof(s_in));
  memset(s_out, 0, sizeof(s_out));
  s_next_id = 1u;
  s_total_rx = 0u;
  s_total_ack = 0u;
}

void msg_store_tick(uint32_t ms)
{
  uint8_t i;
  uint16_t ds = (uint16_t)(ms / 1000u);
  if (ds == 0u) return;
  for (i = 0; i < MSG_INBOX_MAX; i++) {
    if (!s_in[i].used) continue;
    if (s_in[i].age_s < 60000u) s_in[i].age_s = (uint16_t)(s_in[i].age_s + ds);
  }
  for (i = 0; i < MSG_OUTBOX_MAX; i++) {
    if (!s_out[i].used) continue;
    if (s_out[i].age_s < 60000u) s_out[i].age_s = (uint16_t)(s_out[i].age_s + ds);
  }
}

void msg_store_fmt_age(uint16_t age_s, char *buf, uint8_t cap)
{
  if (!buf || cap == 0u) return;
  if (age_s < 60u)        snprintf(buf, cap, "NOW");
  else if (age_s < 3600u) snprintf(buf, cap, "%um", (unsigned)(age_s / 60u));
  else                    snprintf(buf, cap, "%uh", (unsigned)(age_s / 3600u));
}

/* 正文形如 "ack001" / "ack1" 时视为送达确认 */
static int parse_ack(const char *text, uint16_t *id)
{
  uint32_t v = 0;
  int n = 0;
  if (!text) return 0;
  if (text[0] != 'a' || text[1] != 'c' || text[2] != 'k') return 0;
  text += 3;
  while (*text >= '0' && *text <= '9') { v = v * 10u + (uint32_t)(*text - '0'); text++; n++; }
  if (n == 0 || v > 65535u) return 0;
  *id = (uint16_t)v;
  return 1;
}

static int inbox_is_dup(const char *from, uint16_t id)
{
  uint8_t i;
  for (i = 0; i < MSG_INBOX_MAX; i++) {
    if (!s_in[i].used) continue;
    if (s_in[i].id == id && strcmp(s_in[i].from, from) == 0) return 1;
  }
  return 0;
}

uint8_t msg_store_ack(uint16_t id, const char *from)
{
  uint8_t i, k;
  for (i = 0; i < MSG_OUTBOX_MAX; i++) {
    if (!s_out[i].used || s_out[i].id != id) continue;
    s_out[i].status = MSG_ST_ACKED;
    /* 记录 ACK 来源（最多 MSG_ACK_SRC_MAX 个，来自不同中继/电台） */
    for (k = 0; k < s_out[i].ack_count; k++)
      if (strcmp(s_out[i].ack_from[k], from) == 0) { s_total_ack++; return 1u; }
    if (s_out[i].ack_count < MSG_ACK_SRC_MAX) {
      clip(s_out[i].ack_from[s_out[i].ack_count], MSG_ACK_ID_MAX + 1u, from);
      s_out[i].ack_count++;
    }
    s_total_ack++;
    return 1u;
  }
  return 0u;
}

uint8_t msg_store_add_inbox_id(const char *from, const char *text, uint16_t id)
{
  msg_in_t *it;
  uint16_t ack_id;

  if (parse_ack(text, &ack_id)) {          /* ackNNN 不进收件箱，只更新 Sent */
    msg_store_ack(ack_id, from);
    return 0u;
  }
  if (id == 0u) id = s_next_id++;
  if (inbox_is_dup(from, id)) return 0u;

  if (s_in[MSG_INBOX_MAX - 1u].used) s_in[MSG_INBOX_MAX - 1u].used = 0u;  /* 丢最旧 */
  memmove(&s_in[1], &s_in[0], sizeof(msg_in_t) * (MSG_INBOX_MAX - 1u));
  it = &s_in[0];
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->unread = 1u;
  it->id = id;
  it->age_s = 0u;
  clip(it->from, MSG_CALL_MAX + 1u, from);
  clip(it->text, MSG_TEXT_MAX + 1u, text);
  s_total_rx++;
  return 1u;
}

uint8_t msg_store_add_inbox(const char *from, const char *text, uint16_t id)
{
  return msg_store_add_inbox_id(from, text, id);
}

void msg_store_add_outbox(const char *to, const char *text, uint16_t id)
{
  msg_out_t *it;
  if (s_out[MSG_OUTBOX_MAX - 1u].used) s_out[MSG_OUTBOX_MAX - 1u].used = 0u;
  memmove(&s_out[1], &s_out[0], sizeof(msg_out_t) * (MSG_OUTBOX_MAX - 1u));
  it = &s_out[0];
  memset(it, 0, sizeof(*it));
  it->used = 1u;
  it->status = MSG_ST_PENDING;
  it->id = id ? id : s_next_id++;
  it->age_s = 0u;
  clip(it->to, MSG_CALL_MAX + 1u, to);
  clip(it->text, MSG_TEXT_MAX + 1u, text);
}

uint8_t  msg_store_count_inbox(void)  { uint8_t i, n = 0; for (i = 0; i < MSG_INBOX_MAX; i++)  if (s_in[i].used)  n++; return n; }
uint8_t  msg_store_count_outbox(void) { uint8_t i, n = 0; for (i = 0; i < MSG_OUTBOX_MAX; i++) if (s_out[i].used) n++; return n; }
uint16_t msg_store_total_rx(void)     { return s_total_rx; }
uint16_t msg_store_total_ack(void)    { return s_total_ack; }

uint8_t msg_store_unread(void)
{
  uint8_t i, n = 0;
  for (i = 0; i < MSG_INBOX_MAX; i++) if (s_in[i].used && s_in[i].unread) n++;
  return n;
}
uint8_t msg_store_has_unread(void) { return (msg_store_unread() > 0u) ? 1u : 0u; }

msg_in_t  *msg_store_inbox(uint8_t i)  { return (i < MSG_INBOX_MAX)  ? &s_in[i]  : 0; }
msg_out_t *msg_store_outbox(uint8_t i) { return (i < MSG_OUTBOX_MAX) ? &s_out[i] : 0; }

void msg_store_mark_read(uint8_t i)
{
  if (i < MSG_INBOX_MAX && s_in[i].used) s_in[i].unread = 0u;
}

void msg_store_delete_inbox(uint8_t i)
{
  if (i >= MSG_INBOX_MAX || !s_in[i].used) return;
  if ((uint16_t)(i + 1u) < MSG_INBOX_MAX)
    memmove(&s_in[i], &s_in[i + 1u], sizeof(msg_in_t) * (size_t)(MSG_INBOX_MAX - i - 1u));
  memset(&s_in[MSG_INBOX_MAX - 1u], 0, sizeof(msg_in_t));
}

void msg_store_delete_outbox(uint8_t i)
{
  if (i >= MSG_OUTBOX_MAX || !s_out[i].used) return;
  if ((uint16_t)(i + 1u) < MSG_OUTBOX_MAX)
    memmove(&s_out[i], &s_out[i + 1u], sizeof(msg_out_t) * (size_t)(MSG_OUTBOX_MAX - i - 1u));
  memset(&s_out[MSG_OUTBOX_MAX - 1u], 0, sizeof(msg_out_t));
}

/* 演示数据：只造收件箱（Sent/草稿已从 UI 移除，本项目不发射） */
void msg_store_add_demo(void)
{
  msg_store_add_inbox_id("BG5BLH", "net at 19:30 145.050", 11u);
  s_in[0].age_s = 8u;
  msg_store_add_inbox_id("BG5AOZ", "QSO 145.050MHz", 12u);
  s_in[0].age_s = 320u;
  msg_store_add_inbox_id("BY4SZ", "iGate 13.2V", 13u);
  s_in[0].age_s = 7500u;
}