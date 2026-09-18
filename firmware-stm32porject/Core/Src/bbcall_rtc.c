/*
 * STM32F103 片内 RTC（寄存器级）：32 位秒计数器 + 备份域标记。
 *
 * 为什么不走 HAL：CubeIDE 工程没有把 stm32f1xx_hal_rtc.c 加进构建（HAL_RTC_MODULE_ENABLED 也是关的），
 * 这里按 RM0008 第 18 章直接写寄存器，省得动 CubeMX 工程；同时只用一个秒计数器，年月日换算在 rtc_math.c。
 *
 * 时钟源优先级（LSE 最稳，没有 32.768k 晶振就用主板晶振的 HSE/128，最后才 LSI）：
 *   1. LSE 32.768kHz     - 板上焊了 32.768k 晶振才有；配 VBAT 电池能掉电走时
 *   2. HSE/128 = 62.5kHz - 用主板 8MHz 晶振，精度够（主晶振多少 ppm 就是多少），掉电停
 *   3. LSI ~40kHz        - F103 的 LSI 实测 30~60kHz，误差极大，只做最后兜底
 *
 * 备份寄存器：DR1 = 时间已由 PC 对过（值可信），DR2 = 本固件已完成初始化（普通复位后据此跳过重新配置）。
 * 掉电（无 VBAT 电池）后备份域一起丢，属于预期：重新上电用 tools/set_rtc_time.ps1 再对一次即可。
 */
#include "main.h"
#include "bbcall_cfg.h"
#include "bbcall_rtc.h"

#define RTC_MAGIC_VALID  0x5A5Au   /* BKP->DR1：时间已对过 */
#define RTC_MAGIC_INIT   0xA5A5u   /* BKP->DR2：时钟源/预分频已配好 */

static uint8_t s_src = RTC_SRC_NONE;
static int16_t s_trim;   /* ppm：正 = 走快了，分频比按同比例加大 */

/* 等 RTC 寄存器写完成（RTOFF=1 才能改 CNF） */
static void rtc_wait_rtoff(void)
{
  uint32_t guard = 0u;
  while (((RTC->CRL & RTC_CRL_RTOFF) == 0u) && (guard++ < 2000000u)) { }
}

/* 等 APB1 影子寄存器与 RTC 时钟同步（F1 读 CNT 前必须先同步，否则读到旧值） */
static void rtc_wait_rsf(void)
{
  uint32_t guard = 0u;
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  while (((RTC->CRL & RTC_CRL_RSF) == 0u) && (guard++ < 2000000u)) { }
}

/* 时钟源基准频率（Hz）：LSE 32.768k / HSE-128 62.5k / LSI 名义 40k */
static uint32_t rtc_base_hz(uint8_t src)
{
  if (src == RTC_SRC_LSE) return 32768u;
  if (src == RTC_SRC_HSE) return 62500u;
  if (src == RTC_SRC_LSI) return 40000u;
  return 0u;
}

/* 按当前 trim 重写预分频（改分频比不会动计数器，时间不跳） */
static void rtc_write_prescaler(void)
{
  uint32_t base = rtc_base_hz(s_src);
  uint32_t psc;
  if (base == 0u) return;
  psc = rtc_prescaler_for(base, s_trim);
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->PRLH = (uint16_t)((psc >> 16) & 0xFFFFu);
  RTC->PRLL = (uint16_t)(psc & 0xFFFFu);
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  rtc_wait_rtoff();
}
static uint8_t rtc_src_from_bdcr(void)
{
  uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;
  if (sel == RCC_BDCR_RTCSEL_LSE) return RTC_SRC_LSE;
  if (sel == RCC_BDCR_RTCSEL_HSE) return RTC_SRC_HSE;
  if (sel == RCC_BDCR_RTCSEL_LSI) return RTC_SRC_LSI;
  return RTC_SRC_NONE;
}

void bbcall_rtc_init(void)
{

  /* 备份域要能改：PWR/BKP 时钟 + 解除写保护 */
  RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
  PWR->CR |= PWR_CR_DBP;

  /* 已经初始化过（普通复位、或掉电后备份域还有电）：保持走时，不动时钟源和计数器 */
  if ((BKP->DR2 == RTC_MAGIC_INIT) && (RCC->BDCR & RCC_BDCR_RTCEN) && (rtc_src_from_bdcr() != RTC_SRC_NONE)) {
    s_src = rtc_src_from_bdcr();
    s_trim = (int16_t)BKP->DR5;
    rtc_wait_rsf();
    return;
  }

  /* 首次上电 / 备份域已丢：复位备份域，RTC 计数器与 BKP 全清（时间稍后由 PC 对时写入） */
  RCC->BDCR |= RCC_BDCR_BDRST;
  for (volatile uint32_t i = 0u; i < 1000u; i++) { }
  RCC->BDCR &= ~RCC_BDCR_BDRST;

  /* 选时钟源：LSE -> HSE/128 -> LSI */
  RCC->BDCR |= RCC_BDCR_LSEON;
  {
    uint32_t t0 = HAL_GetTick();
    while (((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) && ((uint32_t)(HAL_GetTick() - t0) < 1000u)) { }
  }
  if (RCC->BDCR & RCC_BDCR_LSERDY) {
    s_src = RTC_SRC_LSE;
  } else {
    RCC->BDCR &= ~RCC_BDCR_LSEON;          /* 没有 32.768k 晶振：别让 LSE 继续扒着 PC14/PC15 */
    if (RCC->CR & RCC_CR_HSERDY) {
      s_src = RTC_SRC_HSE;                  /* HSE/128 = 62.5kHz，/62500 = 1Hz */
    } else {
      RCC->CSR |= RCC_CSR_LSION;
      uint32_t t0 = HAL_GetTick();
      while (((RCC->CSR & RCC_CSR_LSIRDY) == 0u) && ((uint32_t)(HAL_GetTick() - t0) < 300u)) { }
      s_src = (RCC->CSR & RCC_CSR_LSIRDY) ? RTC_SRC_LSI : RTC_SRC_NONE;
    }
  }
  if (s_src == RTC_SRC_NONE) return;        /* 没有任何可用时钟源：RTC 不可用（界面退回开机时长） */

  RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL_Msk)
            | ((s_src == RTC_SRC_LSE) ? RCC_BDCR_RTCSEL_LSE
             : (s_src == RTC_SRC_HSE) ? RCC_BDCR_RTCSEL_HSE
             : RCC_BDCR_RTCSEL_LSI)
            | RCC_BDCR_RTCEN;

  rtc_wait_rsf();

  s_trim = (int16_t)BBCALL_RTC_TRIM_PPM;    /* 首次配置：用编译期默认值，之后可用串口 TRIM= 改 */
  rtc_write_prescaler();
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->CNTH = 0u;
  RTC->CNTL = 0u;
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  rtc_wait_rtoff();

  BKP->DR5 = (uint16_t)s_trim;              /* 校准值：与 RTC 一起放在备份域，复位不丢 */
  BKP->DR1 = 0u;                            /* 还没对时 */
  BKP->DR2 = RTC_MAGIC_INIT;
  BKP->DR3 = 0u;
  BKP->DR4 = 0u;
}

uint8_t bbcall_rtc_clock_src(void)
{
  return s_src;
}

const char *bbcall_rtc_clock_src_name(void)
{
  switch (s_src) {
    case RTC_SRC_LSE: return "LSE";
    case RTC_SRC_HSE: return "HSE/128";
    case RTC_SRC_LSI: return "LSI";
    default:          return "none";
  }
}

uint8_t bbcall_rtc_valid(void)
{
  if (s_src == RTC_SRC_NONE) return 0u;
  return (BKP->DR1 == RTC_MAGIC_VALID) ? 1u : 0u;
}

uint8_t bbcall_rtc_get(rtc_dt_t *dt)
{
  uint32_t cnt;
  if (!dt || s_src == RTC_SRC_NONE) return 0u;
  rtc_wait_rsf();
  cnt = ((uint32_t)RTC->CNTH << 16) | (uint32_t)RTC->CNTL;
  rtc_epoch2000_to_dt(cnt, dt);
  return 1u;
}

void bbcall_rtc_set(const rtc_dt_t *dt)
{
  uint32_t cnt;
  if (!dt || s_src == RTC_SRC_NONE || !rtc_dt_valid(dt)) return;
  PWR->CR |= PWR_CR_DBP;                    /* 写 BKP 前再确保一次写保护已解除 */
  cnt = rtc_dt_to_epoch2000(dt);
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->CNTH = (uint16_t)((cnt >> 16) & 0xFFFFu);
  RTC->CNTL = (uint16_t)(cnt & 0xFFFFu);
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  rtc_wait_rtoff();
  BKP->DR3 = dt->year;
  BKP->DR4 = (uint16_t)(((uint16_t)dt->mon << 8) | dt->day);
  BKP->DR1 = RTC_MAGIC_VALID;               /* 最后置有效标记：前三条写完才算对时成功 */
}

#if BBCALL_RTC_SEED_BUILD_TIME
/* 没焊串口时的兜底：把编译时间戳（__DATE__ = "Sep 18 2026"，__TIME__ = "00:45:12"）写进 RTC，
 * 值就是 CubeIDE 点 Build 的那一刻。接上串口后用 tools/set_rtc_time.ps1 可覆盖成精确时间。 */
uint8_t bbcall_rtc_build_time(rtc_dt_t *dt)
{
  return rtc_parse_build_stamp(__DATE__, __TIME__, dt);
}

uint8_t bbcall_rtc_seed_build_time(void)
{
  rtc_dt_t dt;
  if (s_src == RTC_SRC_NONE) return 0u;
  if (!bbcall_rtc_build_time(&dt)) return 0u;
  bbcall_rtc_set(&dt);
  return bbcall_rtc_valid();
}
#endif /* BBCALL_RTC_SEED_BUILD_TIME */

int16_t bbcall_rtc_get_trim(void)
{
  return s_trim;
}

void bbcall_rtc_set_trim(int16_t ppm)
{
  if (ppm > 5000) ppm = 5000;
  if (ppm < -5000) ppm = -5000;
  s_trim = ppm;
  PWR->CR |= PWR_CR_DBP;
  BKP->DR5 = (uint16_t)ppm;
  if (s_src != RTC_SRC_NONE) rtc_write_prescaler();
}

uint32_t bbcall_rtc_divider(void)
{
  rtc_wait_rsf();
  return (((uint32_t)RTC->PRLH << 16) | (uint32_t)RTC->PRLL) + 1u;
}
uint32_t bbcall_rtc_day_ms(void)
{
  rtc_dt_t dt;
  if (!bbcall_rtc_get(&dt)) return 0u;
  return ((uint32_t)dt.hour * 3600u + (uint32_t)dt.min * 60u + (uint32_t)dt.sec) * 1000u;
}