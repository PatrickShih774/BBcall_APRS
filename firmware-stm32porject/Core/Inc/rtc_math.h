/* 纯整数公历日期时间换算，不依赖 HAL/寄存器。
 * STM32F1 的 RTC 只有一个 32 位秒计数器，年月日时分秒全靠这里换算；
 * 固件与主机测试共用：tools/test_rtc_math.c 直接 include 对应的 .c 自测。
 * 算法是 Howard Hinnant 的 days_from_civil / civil_from_days（公有领域），整数除法，
 * 1970-01-01 为第 0 天，负数天数也能正确处理。 */
#ifndef RTC_MATH_H
#define RTC_MATH_H

#include <stdint.h>

/* RTC 计数器基准：2000-01-01 00:00:00 起的秒数（STM32F1 HAL 的约定） */
#define RTC_EPOCH_DAYS_2000  10957   /* 1970-01-01 到 2000-01-01 的天数 */

typedef struct {
  uint16_t year;   /* 完整年份，本工程只支持 2000..2099 */
  uint8_t  mon;    /* 1..12 */
  uint8_t  day;    /* 1..31 */
  uint8_t  hour;   /* 0..23 */
  uint8_t  min;    /* 0..59 */
  uint8_t  sec;    /* 0..59 */
  uint8_t  wday;   /* 0=周日 1=周一 ... 6=周六（由日期算出，不由外部输入） */
} rtc_dt_t;

/* 1970-01-01 起的天数（可为负） */
int32_t  rtc_days_from_civil(int year, unsigned mon, unsigned day);
void     rtc_civil_from_days(int32_t z, int *year, unsigned *mon, unsigned *day);
/* 0=周日 */
uint8_t  rtc_wday_from_date(int year, unsigned mon, unsigned day);
uint8_t  rtc_days_in_month(int year, unsigned mon);
uint8_t  rtc_dt_valid(const rtc_dt_t *dt);
/* 天/秒换算（2000-01-01 基准） */
uint32_t rtc_dt_to_epoch2000(const rtc_dt_t *dt);
void     rtc_epoch2000_to_dt(uint32_t sec, rtc_dt_t *dt);
/* 解析 "YYYY-MM-DD HH:MM:SS"（日期与时间之间空格或 T/t 均可，首尾空白允许） */
uint8_t  rtc_parse_dt(const char *s, rtc_dt_t *dt);
/* 格式化 "2026-09-18 22:30:00"（buf 至少 20 字节） */
void     rtc_format_dt(const rtc_dt_t *dt, char *buf, uint8_t cap);

#endif /* RTC_MATH_H */