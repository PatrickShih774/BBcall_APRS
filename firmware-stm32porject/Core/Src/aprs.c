/* APRS 消息 + Mic-E 位置解析（主机验证参考 aprslib 算法） */
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


/* ---------------- 普通 APRS 位置（非压缩格式） ---------------- */

uint8_t aprs_parse_position(const uint8_t *info, uint16_t len, aprs_position_t *p)
{
  if (!info || !p || len < 19u) return 0;
  uint8_t t = info[0];
  uint16_t s;
  if (t == '!' || t == '=') s = 1u;          /* 无时间戳 */
  else if (t == '/' || t == '@') s = 8u;     /* 7 字节时间戳 */
  else return 0;
  if (len < (uint16_t)(s + 19u)) return 0;
  if (!(info[s] >= '0' && info[s] <= '9')) return 0;   /* 压缩格式暂不支持 */

  memset(p, 0, sizeof(*p));
  /* 纬度 ddmm.hhN/S */
  for (uint8_t i = 0; i < 8u; i++) p->lat[i] = (char)info[s + i];
  p->lat[8] = '\0';
  /* 经度 dddmm.hhE/W（前面还有 1 字节符号表） */
  for (uint8_t i = 0; i < 9u; i++) p->lon[i] = (char)info[s + 9u + i];
  p->lon[9] = '\0';

  uint16_t ci = (uint16_t)(s + 19u);
  uint16_t cl = 0;
  while (ci < len && cl < (uint16_t)(sizeof(p->comment) - 1u)) {
    uint8_t ch = info[ci++];
    if (ch == '\r' || ch == '\n') break;
    p->comment[cl++] = (char)ch;
  }
  while (cl > 0 && p->comment[cl - 1] == ' ') cl--;
  p->comment[cl] = '\0';
  p->valid = 1;
  return 1;
}/* ---------------- Mic-E ---------------- */

static uint8_t dig2(const char *p)
{
  uint8_t a = (p[0] >= '0' && p[0] <= '9') ? (uint8_t)(p[0] - '0') : 0u;
  uint8_t b = (p[1] >= '0' && p[1] <= '9') ? (uint8_t)(p[1] - '0') : 0u;
  return (uint8_t)(a * 10u + b);
}

static void put2(char *d, uint8_t v)
{
  d[0] = (char)('0' + (v / 10u) % 10u);
  d[1] = (char)('0' + v % 10u);
}

static void put3(char *d, uint16_t v)
{
  d[0] = (char)('0' + (v / 100u) % 10u);
  d[1] = (char)('0' + (v / 10u) % 10u);
  d[2] = (char)('0' + v % 10u);
}

static const char *const MT_STD[8] = {
  "M0: Off Duty", "M1: En Route", "M2: In Service", "M3: Returning",
  "M4: Committed", "M5: Special", "M6: Priority", "Emergency"
};
static const char *const MT_CUSTOM[8] = {
  "C0: Custom-0", "C1: Custom-1", "C2: Custom-2", "C3: Custom-3",
  "C4: Custom-4", "C5: Custom-5", "C6: Custom-6", "Emergency"
};

uint8_t aprs_parse_mice(const char *dest, const uint8_t *info, uint16_t len, aprs_mice_t *m)
{
  if (!dest || !info || !m) return 0;
  if (info[0] != '`' && info[0] != '\'') return 0;
  if (len < 9u) return 0;
  if (strlen(dest) != 6u) return 0;
  memset(m, 0, sizeof(*m));

  const uint8_t *b = info + 1;          /* Mic-E 正文（经度/速度/航向/符号） */
  uint16_t body_len = (uint16_t)(len - 1u);

  /* 纬度：目标呼号 6 字符编码 */
  char tmp[7];
  for (uint8_t i = 0; i < 6u; i++) {
    char c = dest[i];
    if (c == 'K' || c == 'L' || c == 'Z') tmp[i] = ' ';
    else if (c > 'L') tmp[i] = (char)(c - 32);
    else if (c > '9') tmp[i] = (char)(c - 17);
    else tmp[i] = c;
  }
  tmp[6] = '\0';
  uint8_t amb = 0;
  for (int i = 5; i >= 0 && tmp[i] == ' '; i--) amb++;
  if (amb >= 4u) tmp[2] = '3';
  else if (amb > 0u) tmp[6 - amb] = '5';

  uint8_t lat_deg = dig2(tmp);
  uint8_t lat_min = dig2(tmp + 2);
  uint8_t lat_frac = dig2(tmp + 4);
  char ns = (dest[3] <= 'L') ? 'S' : 'N';
  put2(m->lat, lat_deg);
  put2(m->lat + 2, lat_min);
  m->lat[4] = '.';
  put2(m->lat + 5, lat_frac);
  m->lat[7] = ns;
  m->lat[8] = '\0';

  /* 消息类型：目标呼号前 3 字符 */
  uint8_t idx = 0;
  uint8_t custom = 0;
  for (uint8_t i = 0; i < 3u; i++) {
    char c = dest[i];
    char mb;
    if ((c >= '0' && c <= '9') || c == 'L') mb = '0';
    else if (c >= 'P' && c <= 'Z') mb = '1';
    else { mb = '2'; custom = 1; }
    if (mb == '1') idx |= (uint8_t)(1u << (2u - i));
  }
  {
    const char *mt = custom ? MT_CUSTOM[7u - idx] : MT_STD[7u - idx];
    strncpy(m->mtype, mt, sizeof(m->mtype) - 1u);
    m->mtype[sizeof(m->mtype) - 1u] = '\0';
  }

  /* 经度/速度/航向 */
  int32_t lng = (int32_t)b[0] - 28;
  if (dest[4] >= 'P') lng += 100;
  if (lng >= 180 && lng <= 189) lng -= 80;
  if (lng >= 190 && lng <= 199) lng -= 190;
  if (lng < 0) lng = 0;
  if (lng > 179) lng = 179;

  int32_t lmin = (int32_t)b[1] - 28;
  if (lmin < 0) lmin = 0;
  if (lmin >= 60) lmin -= 60;
  int32_t lfrac = (int32_t)b[2] - 28;
  if (lfrac < 0) lfrac = 0;
  if (lfrac > 99) lfrac = 99;
  int32_t lm100 = lmin * 100 + lfrac;
  if (amb >= 4u) lm100 = 3000;
  else if (amb == 3u) lm100 = ((lm100 + 500) / 1000) * 1000;
  else if (amb == 2u) lm100 = ((lm100 + 50) / 100) * 100;
  else if (amb == 1u) lm100 = ((lm100 + 5) / 10) * 10;
  lmin = lm100 / 100;
  lfrac = lm100 % 100;

  char ew = (dest[5] >= 'P') ? 'W' : 'E';
  put3(m->lon, (uint16_t)lng);
  put2(m->lon + 3, (uint8_t)lmin);
  m->lon[5] = '.';
  put2(m->lon + 6, (uint8_t)lfrac);
  m->lon[8] = ew;
  m->lon[9] = '\0';

  int32_t speed = (int32_t)b[3] - 28;
  if (speed < 0) speed = 0;
  speed *= 10;
  int32_t course = (int32_t)b[4] - 28;
  if (course < 0) course = 0;
  int32_t q = course / 10;
  course = (course % 10) * 100 + ((int32_t)b[5] - 28);
  speed += q;
  if (speed >= 800) speed -= 800;
  if (course >= 400) course -= 400;
  if (speed < 0) speed = 0;
  if (course < 0) course = 0;
  m->speed_kmh = (uint16_t)((speed * 1852 + 500) / 1000);
  m->course = (uint16_t)course;

  m->symbol = b[6];
  m->symbol_table = (char)b[7];

  uint16_t ci = 8u;
  uint16_t cl = 0;
  while (ci < body_len && cl < (uint16_t)(sizeof(m->comment) - 1u)) {
    uint8_t c = b[ci++];
    if (c == '\r' || c == '\n') break;
    m->comment[cl++] = (char)c;
  }
  while (cl > 0 && m->comment[cl - 1] == ' ') cl--;
  m->comment[cl] = '\0';

  m->valid = 1;
  return 1;
}