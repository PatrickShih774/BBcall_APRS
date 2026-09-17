/* rtc_math.c 的主机自测（不碰 HAL/寄存器，直接在 PC 上跑）。
 * 用法（仓库根目录）:
 *   third_party\tcc\tcc\x86_64-win32-tcc.exe -run tools\test_rtc_math.c
 * 或 gcc -I firmware-stm32porject/Core/Inc tools/test_rtc_math.c -o t && ./t
 * 覆盖：已知星期/天数、闰年、往返换算、解析（含非法输入）。退出码 0 = 全部通过。 */
#include <stdio.h>
#include <string.h>
#include "../firmware-stm32porject/Core/Src/rtc_math.c"

static int fails = 0;

static void ck(int cond, const char *what)
{
  if (!cond) { printf("FAIL %s\n", what); fails++; }
}

static void ck_u32(uint32_t got, uint32_t want, const char *what)
{
  if (got != want) { printf("FAIL %s: got %lu want %lu\n", what, (unsigned long)got, (unsigned long)want); fails++; }
}

int main(void)
{
  rtc_dt_t dt;
  char buf[24];
  int y;
  unsigned m, d;

  /* 已知基准 */
  ck(rtc_days_from_civil(1970, 1, 1) == 0, "days(1970-01-01)=0");
  ck(rtc_days_from_civil(2000, 1, 1) == 10957, "days(2000-01-01)=10957");
  ck(rtc_wday_from_date(1970, 1, 1) == 4, "1970-01-01 = Thu");
  ck(rtc_wday_from_date(2000, 1, 1) == 6, "2000-01-01 = Sat");
  ck(rtc_wday_from_date(2026, 9, 18) == 5, "2026-09-18 = Fri");
  ck(rtc_days_in_month(2024, 2) == 29, "2024-02 = 29");
  ck(rtc_days_in_month(2026, 2) == 28, "2026-02 = 28");
  ck(rtc_days_in_month(2000, 2) == 29, "2000-02 = 29");
  ck(rtc_days_in_month(1900, 2) == 28, "1900-02 = 28");
  ck(rtc_days_in_month(2026, 13) == 0, "mon=13 -> 0");

  /* civil_from_days 与 days_from_civil 互逆（含负天数） */
  for (int32_t z = -1000; z <= 50000; z += 7) {
    rtc_civil_from_days(z, &y, &m, &d);
    ck(rtc_days_from_civil(y, m, d) == z, "civil<->days roundtrip");
    if (fails) break;
  }

  /* RTC 计数器（2000-01-01 起秒）已知点 */
  ck_u32(rtc_dt_to_epoch2000(&(rtc_dt_t){ .year=2000, .mon=1, .day=1, .hour=0 }), 0u, "epoch2000 origin");
  ck_u32(rtc_dt_to_epoch2000(&(rtc_dt_t){ .year=2026, .mon=9, .day=18, .hour=0 }), 843004800u, "2026-09-18 00:00");
  ck_u32(rtc_dt_to_epoch2000(&(rtc_dt_t){ .year=2026, .mon=9, .day=18, .hour=22, .min=30, .sec=0 }), 843085800u, "2026-09-18 22:30");

  /* 20 年逐日往返（每天取 3 个时刻） */
  for (uint32_t sec = 0u; sec < 20u * 365u * 86400u; sec += 86400u / 3u) {
    rtc_epoch2000_to_dt(sec, &dt);
    if (rtc_dt_to_epoch2000(&dt) != sec) { printf("FAIL roundtrip sec=%lu\n", (unsigned long)sec); fails++; break; }
  }

  /* 解析 */
  ck(rtc_parse_dt("2026-09-18 22:30:00", &dt) && dt.year == 2026u && dt.mon == 9u && dt.day == 18u &&
     dt.hour == 22u && dt.min == 30u && dt.sec == 0u && dt.wday == 5u, "parse normal");
  ck(rtc_parse_dt("2026-09-18T22:30:00", &dt) && dt.sec == 0u, "parse T separator");
  ck(rtc_parse_dt("  2026-9-8 7:05:09  ", &dt) && dt.mon == 9u && dt.day == 8u && dt.hour == 7u, "parse short digits + blanks");
  ck(!rtc_parse_dt("2026-02-29 00:00:00", &dt), "reject 2026-02-29");
  ck(rtc_parse_dt("2024-02-29 00:00:00", &dt), "accept 2024-02-29");
  ck(!rtc_parse_dt("2026-13-01 00:00:00", &dt), "reject mon=13");
  ck(!rtc_parse_dt("2026-09-18 24:00:00", &dt), "reject hour=24");
  ck(!rtc_parse_dt("2026-09-18 22:30", &dt), "reject missing seconds");
  ck(!rtc_parse_dt("2026-09-18 22:30:00 junk", &dt), "reject trailing junk");
  ck(!rtc_parse_dt("TIME=2026-09-18 22:30:00", &dt), "reject with prefix (caller strips it)");
  ck(!rtc_parse_dt("1999-12-31 23:59:59", &dt), "reject year<2000");

  /* 编译时间戳（__DATE__/__TIME__ -> dt）：含日号空格补齐、非法输入 */
  ck(rtc_parse_build_stamp("Sep 18 2026", "00:45:12", &dt) && dt.year == 2026u && dt.mon == 9u &&
     dt.day == 18u && dt.hour == 0u && dt.min == 45u && dt.sec == 12u && dt.wday == 5u, "build stamp normal");
  ck(rtc_parse_build_stamp("Sep  8 2026", "23:59:59", &dt) && dt.day == 8u && dt.hour == 23u, "build stamp 1-digit day");
  ck(rtc_parse_build_stamp("Jan  1 2000", "00:00:00", &dt) && dt.wday == 6u, "build stamp 2000-01-01 Sat");
  ck(!rtc_parse_build_stamp("Xxx 18 2026", "00:00:00", &dt), "build stamp bad month");
  ck(!rtc_parse_build_stamp("Sep 32 2026", "00:00:00", &dt), "build stamp bad day");
  ck(!rtc_parse_build_stamp("Sep 18 2026", "24:00:00", &dt), "build stamp bad hour");
  ck(rtc_parse_build_stamp(__DATE__, __TIME__, &dt), "build stamp real __DATE__/__TIME__");
  /* 格式化 */
  dt.year = 2026u; dt.mon = 9u; dt.day = 18u; dt.hour = 22u; dt.min = 30u; dt.sec = 5u; dt.wday = 5u;
  rtc_format_dt(&dt, buf, (uint8_t)sizeof(buf));
  ck(strcmp(buf, "2026-09-18 22:30:05") == 0, "format");

  if (fails) { printf("RTC MATH TEST: %d FAIL\n", fails); return 1; }
  printf("RTC MATH TEST: all pass\n");
  return 0;
}