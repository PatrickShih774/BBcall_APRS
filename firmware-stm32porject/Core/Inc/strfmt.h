/* 极简字符串拼装，用来替代 snprintf。
 * 为什么不用 snprintf：nano.specs 的 printf 家族在 -O0 下约 2.2KB（_svfprintf_r/__ssputs_r/snprintf），
 * 还会把 malloc/realloc/_sbrk 一起拖进固件（本项目不用堆）；换成这几十行后 Debug 配置也能装下。
 * 所有写入都是截断安全的：始终保证 '\0' 结尾，超出 cap 的部分直接丢弃。 */
#ifndef STRFMT_H
#define STRFMT_H

#include <stdint.h>

typedef struct {
  char   *buf;
  uint8_t cap;   /* 缓冲区总大小（含结尾 '\0'） */
  uint8_t len;   /* 当前已写入长度 */
} sfb_t;

void sfb_init(sfb_t *b, char *buf, uint8_t cap);
void sfb_ch(sfb_t *b, char c);
void sfb_str(sfb_t *b, const char *s);              /* 追加以 '\0' 结尾的串 */
void sfb_strn(sfb_t *b, const char *s, uint8_t n);  /* 最多追加 n 个字符（对应 %.Ns） */
void sfb_u32(sfb_t *b, uint32_t v);                 /* 十进制无符号 */
void sfb_u32w(sfb_t *b, uint32_t v, uint8_t width); /* 零填充到 width 位（对应 %0Nu） */
void sfb_i32(sfb_t *b, int32_t v);                  /* 带符号（对应 %d） */

#endif /* STRFMT_H */