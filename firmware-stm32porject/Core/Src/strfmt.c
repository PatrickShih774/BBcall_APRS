/* 极简字符串拼装（见 strfmt.h）。纯整数、不依赖 newlib，也不分配内存。 */
#include "strfmt.h"

void sfb_init(sfb_t *b, char *buf, uint8_t cap)
{
  b->buf = buf;
  b->cap = cap;
  b->len = 0u;
  if (cap > 0u) buf[0] = '\0';
}

void sfb_ch(sfb_t *b, char c)
{
  if ((uint8_t)(b->len + 1u) >= b->cap) return;   /* 留一个字节给 '\0' */
  b->buf[b->len] = c;
  b->len = (uint8_t)(b->len + 1u);
  b->buf[b->len] = '\0';
}

void sfb_strn(sfb_t *b, const char *s, uint8_t n)
{
  uint8_t i = 0u;
  if (!s) return;
  while (s[i] != '\0' && i < n) { sfb_ch(b, s[i]); i++; }
}

void sfb_str(sfb_t *b, const char *s)
{
  sfb_strn(b, s, 255u);
}

void sfb_u32w(sfb_t *b, uint32_t v, uint8_t width)
{
  char tmp[10];
  uint8_t n = 0u;

  do {
    tmp[n] = (char)('0' + (char)(v % 10u));
    n = (uint8_t)(n + 1u);
    v /= 10u;
  } while (v != 0u && n < (uint8_t)sizeof(tmp));

  while (n < width) { sfb_ch(b, '0'); width--; }   /* 零填充 */
  while (n > 0u) { n--; sfb_ch(b, tmp[n]); }
}

void sfb_u32(sfb_t *b, uint32_t v)
{
  sfb_u32w(b, v, 1u);
}

void sfb_i32(sfb_t *b, int32_t v)
{
  if (v < 0) { sfb_ch(b, '-'); sfb_u32(b, (uint32_t)(-v)); }
  else       { sfb_u32(b, (uint32_t)v); }
}