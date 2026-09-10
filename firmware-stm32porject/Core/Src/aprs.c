/* APRS 消息解析（主机已验证）。 */
#include "aprs.h"
#include <string.h>

uint8_t aprs_parse_message(const uint8_t *info, uint16_t len, aprs_message_t *m)
{
  if (len < 11u || info[0] != ':') return 0;
  memset(m, 0, sizeof(*m));
  uint16_t a = 0;
  for (uint16_t i = 1; i < 10u; i++) {
    uint8_t c = info[i];
    if (c == ' ') continue;
    if (a < (APRS_ADDR_MAX - 1u)) m->addressee[a++] = (char)c;
  }
  m->addressee[a] = '\0';
  m->addressee_len = a;
  uint16_t b = 0;
  for (uint16_t i = 11; i < len; i++) {
    uint8_t c = info[i];
    if (c == '|') {
      m->has_msg_id = 1;
      for (uint16_t k = 0; k < 7u && (i + 1u + k) < len; k++) m->msg_id[k] = info[i + 1u + k];
      break;
    }
    if (b < (APRS_BODY_MAX - 1u)) m->body[b++] = c;
  }
  m->body[b] = 0;
  m->body_len = b;
  return 1;
}
