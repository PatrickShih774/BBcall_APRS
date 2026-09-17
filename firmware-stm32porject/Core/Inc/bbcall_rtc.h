/* STM32F103 片内 RTC：PC 串口对时用（tools/set_rtc_time.ps1 -> TIME=YYYY-MM-DD HH:MM:SS） */
#ifndef BBCALL_RTC_H
#define BBCALL_RTC_H

#include <stdint.h>
#include "rtc_math.h"

/* 时钟源（bbcall_rtc_clock_src() 返回值） */
#define RTC_SRC_NONE 0u   /* 没有可用时钟源，RTC 不走 */
#define RTC_SRC_LSE  1u   /* 外部 32.768kHz 晶振：最准，配 VBAT 电池可掉电走时 */
#define RTC_SRC_HSE  2u   /* HSE/128 = 62.5kHz：用主板 8MHz 晶振，够准，掉电即停 */
#define RTC_SRC_LSI  3u   /* 内部 LSI ~40kHz：F103 的 LSI 误差极大，仅兜底 */

void        bbcall_rtc_init(void);       /* 备份域 + 时钟源 + 预分频；可重复调用 */
uint8_t     bbcall_rtc_clock_src(void);
const char *bbcall_rtc_clock_src_name(void);   /* "LSE" / "HSE/128" / "LSI" / "none" */
uint8_t     bbcall_rtc_valid(void);      /* 1 = 已经对过时（备份寄存器标记） */
uint8_t     bbcall_rtc_get(rtc_dt_t *dt);      /* 读当前时间；0 = 没走过时 */
void        bbcall_rtc_set(const rtc_dt_t *dt); /* 写时间并置"已对时"标记 */
uint32_t    bbcall_rtc_day_ms(void);     /* 当日 0 点起的毫秒数（UI 时钟基准） */

/* 编译时间戳兜底（BBCALL_RTC_SEED_BUILD_TIME=1 时用）：__DATE__/__TIME__ -> dt / 直接写 RTC */
uint8_t     bbcall_rtc_build_time(rtc_dt_t *dt);
uint8_t     bbcall_rtc_seed_build_time(void);

#endif /* BBCALL_RTC_H */