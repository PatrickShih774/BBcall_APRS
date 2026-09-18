/* 纯整数公历日期时间换算（见 rtc_math.h）。与硬件无关，主机上可直接自测。 */
#include "rtc_math.h"
#include <stdio.h>

static int rtc_is_leap(int y)
{
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

int32_t rtc_days_from_civil(int year, unsigned mon, unsigned day)
{
  int y = (mon <= 2u) ? (year - 1) : year;
  int era = (y >= 0 ? y : (y - 399)) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned mp = (mon + 9u) % 12u;                     /* 3 月 = 0 ... 次年 2 月 = 11 */
  unsigned doy = (153u * mp + 2u) / 5u + day - 1u;    /* 年内第几天，3 月 1 日 = 0 */
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return (int32_t)era * 146097 + (int32_t)doe - 719468;
}

void rtc_civil_from_days(int32_t z, int *year, unsigned *mon, unsigned *day)
{
  int32_t era;
  unsigned doe, yoe, doy, mp, d, m;
  int y;
  z += 719468;
  era = (z >= 0 ? z : (z - 146096)) / 146097;
  doe = (unsigned)(z - era * 146097);
  yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  y   = (int)yoe + (int)era * 400;
  doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  mp  = (5u * doy + 2u) / 153u;
  d   = doy - (153u * mp + 2u) / 5u + 1u;
  m   = (mp < 10u) ? (mp + 3u) : (mp - 9u);
  y  += (m <= 2u) ? 1 : 0;
  if (year) *year = y;
  if (mon)  *mon  = m;
  if (day)  *day  = d;
}

uint8_t rtc_wday_from_date(int year, unsigned mon, unsigned day)
{
  int32_t days = rtc_days_from_civil(year, mon, day);   /* 1970-01-01 = 周四 */
  int32_t w = (days + 4) % 7;
  if (w < 0) w += 7;
  return (uint8_t)w;
}

uint8_t rtc_days_in_month(int year, unsigned mon)
{
  static const uint8_t dim[12] = { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };
  if (mon < 1u || mon > 12u) return 0u;
  if (mon == 2u && rtc_is_leap(year)) return 29u;
  return dim[mon - 1u];
}

uint8_t rtc_dt_valid(const rtc_dt_t *dt)
{
  if (!dt) return 0u;
  if (dt->year < 2000u || dt->year > 2099u) return 0u;
  if (dt->mon < 1u || dt->mon > 12u) return 0u;
  if (dt->day < 1u || dt->day > rtc_days_in_month((int)dt->year, dt->mon)) return 0u;
  if (dt->hour > 23u || dt->min > 59u || dt->sec > 59u) return 0u;
  return 1u;
}

uint32_t rtc_dt_to_epoch2000(const rtc_dt_t *dt)
{
  int32_t days = rtc_days_from_civil((int)dt->year, dt->mon, dt->day) - RTC_EPOCH_DAYS_2000;
  uint32_t sod = (uint32_t)dt->hour * 3600u + (uint32_t)dt->min * 60u + dt->sec;
  return (uint32_t)days * 86400u + sod;     /* 先转 uint32 再乘，避免 int32 溢出 */
}

void rtc_epoch2000_to_dt(uint32_t sec, rtc_dt_t *dt)
{
  uint32_t days, sod;
  int y = 2000;
  unsigned m = 1u, d = 1u;
  if (!dt) return;
  days = sec / 86400u;
  sod  = sec % 86400u;
  rtc_civil_from_days((int32_t)days + RTC_EPOCH_DAYS_2000, &y, &m, &d);
  dt->year = (uint16_t)y;
  dt->mon  = (uint8_t)m;
  dt->day  = (uint8_t)d;
  dt->hour = (uint8_t)(sod / 3600u);
  dt->min  = (uint8_t)((sod / 60u) % 60u);
  dt->sec  = (uint8_t)(sod % 60u);
  dt->wday = rtc_wday_from_date(y, m, d);
}

static const char *skip_blank(const char *s)
{
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

static uint8_t parse_num(const char **ps, uint8_t min_digits, uint8_t max_digits, uint16_t *out)
{
  const char *s = *ps;
  uint16_t v = 0u;
  uint8_t n = 0u;
  while (n < max_digits && s[n] >= '0' && s[n] <= '9') {
    v = (uint16_t)(v * 10u + (uint16_t)(s[n] - '0'));
    n++;
  }
  if (n < min_digits) return 0u;
  *ps = s + n;
  *out = v;
  return 1u;
}

uint8_t rtc_parse_dt(const char *s, rtc_dt_t *dt)
{
  rtc_dt_t t;
  uint16_t v;
  if (!s || !dt) return 0u;
  s = skip_blank(s);
  if (!parse_num(&s, 4u, 4u, &t.year)) return 0u;     /* 年份必须写满 4 位 */
  if (*s++ != '-') return 0u;
  if (!parse_num(&s, 1u, 2u, &v)) return 0u;
  t.mon = (uint8_t)v;
  if (*s++ != '-') return 0u;
  if (!parse_num(&s, 1u, 2u, &v)) return 0u;
  t.day = (uint8_t)v;
  if (*s != ' ' && *s != 'T' && *s != 't') return 0u;
  s++;
  if (!parse_num(&s, 1u, 2u, &v)) return 0u;
  t.hour = (uint8_t)v;
  if (*s++ != ':') return 0u;
  if (!parse_num(&s, 1u, 2u, &v)) return 0u;
  t.min = (uint8_t)v;
  if (*s++ != ':') return 0u;
  if (!parse_num(&s, 1u, 2u, &v)) return 0u;
  t.sec = (uint8_t)v;
  s = skip_blank(s);
  if (*s != '\0' && *s != '\r' && *s != '\n') return 0u;
  t.wday = rtc_wday_from_date((int)t.year, t.mon, t.day);
  if (!rtc_dt_valid(&t)) return 0u;
  *dt = t;
  return 1u;
}

uint32_t rtc_prescaler_for(uint32_t base_hz, int16_t ppm)
{
  int32_t n, adj;
  if (base_hz < 2u) return 1u;
  if (ppm > 5000) ppm = 5000;              /* 限幅 ±0.5%：再大说明时钟源本身有问题，不是校准能救的 */
  if (ppm < -5000) ppm = -5000;
  n = (int32_t)base_hz;
  adj = (n * (int32_t)ppm) / 1000000;      /* n <= 62500、|ppm| <= 5000，最大 312500，不会溢出 */
  n += adj;
  if (n < 2) n = 2;
  return (uint32_t)(n - 1);
}

int32_t rtc_trim_effective_ppm(uint32_t base_hz, int16_t ppm)
{
  if (base_hz < 2u) return 0;
  return (int32_t)(((int32_t)(rtc_prescaler_for(base_hz, ppm) + 1u) - (int32_t)base_hz) * 1000000)
         / (int32_t)base_hz;
}
uint8_t rtc_parse_build_stamp(const char *date, const char *time, rtc_dt_t *dt)
{
  static const char MN[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  rtc_dt_t x;
  uint8_t mon = 0u;

  if (!date || !time) return 0u;
  for (uint8_t i = 0u; i < 12u; i++) {
    if (date[0] == MN[i][0] && date[1] == MN[i][1] && date[2] == MN[i][2]) { mon = (uint8_t)(i + 1u); break; }
  }
  if (mon == 0u) return 0u;
  for (uint8_t i = 0u; i < 3u; i++) {           /* 日号必须是数字或空格，防住畸形 __DATE__ */
    if ((date[4 + i] < '0' || date[4 + i] > '9') && date[4 + i] != ' ') return 0u;
  }

  x.mon  = mon;
  x.day  = (uint8_t)((unsigned)((date[4] == ' ') ? 0 : (date[4] - '0') * 10) + (unsigned)(date[5] - '0'));
  x.year = (uint16_t)((unsigned)(date[7]  - '0') * 1000u + (unsigned)(date[8]  - '0') * 100u
                    + (unsigned)(date[9]  - '0') * 10u   + (unsigned)(date[10] - '0'));
  x.hour = (uint8_t)((unsigned)(time[0] - '0') * 10u + (unsigned)(time[1] - '0'));
  x.min  = (uint8_t)((unsigned)(time[3] - '0') * 10u + (unsigned)(time[4] - '0'));
  x.sec  = (uint8_t)((unsigned)(time[6] - '0') * 10u + (unsigned)(time[7] - '0'));
  x.wday = rtc_wday_from_date((int)x.year, x.mon, x.day);
  if (!rtc_dt_valid(&x)) return 0u;
  if (dt) *dt = x;
  return 1u;
}
void rtc_format_dt(const rtc_dt_t *dt, char *buf, uint8_t cap)
{
  if (!buf || cap < 20u) { if (buf && cap) buf[0] = '\0'; return; }
  snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u:%02u",
           (unsigned)dt->year, (unsigned)dt->mon, (unsigned)dt->day,
           (unsigned)dt->hour, (unsigned)dt->min, (unsigned)dt->sec);
}