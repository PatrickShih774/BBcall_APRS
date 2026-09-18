/* BBcall 应用：初始化 + 主循环（无硬件串口；状态经 LED/LCD）。 */
#include "main.h"
#include "bbcall_cfg.h"
#include "bbcall_hw.h"
#include "bbcall_app.h"
#include "bk4802.h"
#include "bbcall_rtc.h"
#include "lcd_st7567.h"
#include "ui_harness.h"

#include "modem.h"
#include "ax25.h"
#include "aprs.h"
#include <string.h>

/* ---------------- PC 对时（片内 RTC + 串口命令） ----------------
 * 上位机 tools/set_rtc_time.ps1 发 "TIME=YYYY-MM-DD HH:MM:SS"，固件写 RTC 并把 UI 时钟
 * 立刻切到新时间；"TIME?" 回读当前时间（脚本用它找设备、核对结果）。
 * 命令很短也很少，用一行 40 字节缓冲 + 逐字符状态机，不加动态内存。 */
#define CONSOLE_LINE_MAX 40u
static char    s_cmd_line[CONSOLE_LINE_MAX];
static uint8_t s_cmd_len;

static void rtc_apply_to_ui(void)
{
#if BBCALL_LCD_ENABLED
  rtc_dt_t dt;
  if (!bbcall_rtc_get(&dt)) return;
  ui_set_wallclock(dt.wday, dt.mon, dt.day);   /* 锁屏页第二行：周W M/D */
  ui_set_clock_ms(bbcall_rtc_day_ms());        /* 设备时钟切到当天 0 点起的毫秒数 */
#else
  /* 没有 LCD：RTC 只通过串口读写 */
#endif
}

static const char *const RTC_WDAY_NAME[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

/* 有符号 ppm 打印（hw_console_u16 只管无符号） */
static void console_i16(int16_t v)
{
  if (v < 0) { hw_console_putc('-'); hw_console_u16((uint16_t)(-v)); }
  else       { hw_console_u16((uint16_t)v); }
}

static uint32_t rtc_base_hz(void)
{
  switch (bbcall_rtc_clock_src()) {
    case RTC_SRC_LSE: return 32768u;
    case RTC_SRC_HSE: return 62500u;
    case RTC_SRC_LSI: return 40000u;
    default:          return 0u;
  }
}

/* "[RTC] ... trim=+520ppm (eff=+512ppm) div=62532" */
static void rtc_print_trim(void)
{
  hw_console_puts(" trim=");
  console_i16(bbcall_rtc_get_trim());
  hw_console_puts("ppm (eff=");
  console_i16((int16_t)rtc_trim_effective_ppm(rtc_base_hz(), bbcall_rtc_get_trim()));
  hw_console_puts("ppm) div=");
  hw_console_u32(bbcall_rtc_divider());
}

/* 解析 "±NNNN"（ppm），成功返回 1 */
static uint8_t parse_ppm(const char *s, int16_t *out)
{
  int32_t v = 0;
  int sign = 1;
  uint8_t n = 0u;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '+') s++;
  else if (*s == '-') { sign = -1; s++; }
  while (*s >= '0' && *s <= '9' && n < 4u) { v = v * 10 + (*s - '0'); s++; n++; }
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  if (n == 0u || *s != '\0') return 0u;
  v *= sign;
  if (v > 5000) v = 5000;
  if (v < -5000) v = -5000;
  *out = (int16_t)v;
  return 1u;
}
static void rtc_print_dt(const char *tag, const rtc_dt_t *dt)
{
  char buf[20];
  rtc_format_dt(dt, buf, (uint8_t)sizeof(buf));
  hw_console_puts(tag);
  hw_console_puts(buf);
  hw_console_puts(" ");
  hw_console_puts(RTC_WDAY_NAME[dt->wday % 7u]);
  hw_console_puts(" src=");
  hw_console_puts(bbcall_rtc_clock_src_name());
  hw_console_puts("\r\n");
}

static void console_cmd_apply(const char *line)
{
  const char *s = line;
  rtc_dt_t dt;

  while (*s == ' ' || *s == '\t') s++;
  if (*s == '\0') return;

  if (strncmp(s, "TRIM?", 5u) == 0) {
    hw_console_puts("[RTC] src=");
    hw_console_puts(bbcall_rtc_clock_src_name());
    rtc_print_trim();
    hw_console_puts("\r\n");
    return;
  }
  if (strncmp(s, "TRIM=", 5u) == 0) {
    int16_t ppm;
    if (!parse_ppm(s + 5, &ppm)) { hw_console_puts("[RTC] err: use TRIM=+520 / TRIM=-120 (ppm)\r\n"); return; }
    bbcall_rtc_set_trim(ppm);
    hw_console_puts("[RTC] trim set");
    rtc_print_trim();
    hw_console_puts("\r\n");
    return;
  }  if (strncmp(s, "TIME?", 5u) == 0) {
    if (bbcall_rtc_get(&dt) && bbcall_rtc_valid()) rtc_print_dt("[RTC] now ", &dt);
    else hw_console_puts("[RTC] no-time (send TIME=YYYY-MM-DD HH:MM:SS)\r\n");
    return;
  }
  if (strncmp(s, "TIME=", 5u) == 0)              s += 5;
  else if (strncmp(s, "SETTIME=", 8u) == 0)      s += 8;
  else { hw_console_puts("[RTC] err: use TIME=YYYY-MM-DD HH:MM:SS or TIME?\r\n"); return; }

  if (!rtc_parse_dt(s, &dt)) {
    hw_console_puts("[RTC] err: bad format, want TIME=YYYY-MM-DD HH:MM:SS\r\n");
    return;
  }
  if (bbcall_rtc_clock_src() == RTC_SRC_NONE) {
    hw_console_puts("[RTC] err: no clock source\r\n");
    return;
  }
  bbcall_rtc_set(&dt);
  rtc_apply_to_ui();                     /* 不用等 1s 同步，命令里立刻生效 */
  rtc_print_dt("[RTC] set ", &dt);
}

static void console_poll(void)
{
  char c;
  while (hw_console_try_getc(&c)) {
    if (c == '\r' || c == '\n') {
      if (s_cmd_len) { s_cmd_line[s_cmd_len] = '\0'; console_cmd_apply(s_cmd_line); s_cmd_len = 0u; }
    } else if (c >= 0x20 && c <= 0x7E) {   /* 只收可见 ASCII，杂散字节直接丢 */
      if (s_cmd_len + 1u < CONSOLE_LINE_MAX) s_cmd_line[s_cmd_len++] = c;
      else s_cmd_len = 0u;                 /* 行太长：整行丢弃，等下一条 */
    }
  }
}

#if BBCALL_LCD_ENABLED
/* ---------------- 三键扫描与背光（design.md §9：只有 ▲ ▼ ●） ----------------
 * 按键上拉输入、按下为低。消抖：10ms 采样、连续 3 次一致才采信。
 * ▲▼：按下沿触发一次，按住 0.6s 后 0.18s/次自动重复（收件箱翻条）；
 * ● ：松开时按按住时长区分短按 / 长按（≥620ms = 退出），与模拟器 lcd_sim 翻译规则一致。 */
static uint32_t s_bl_off_at;   /* 背光熄灭时刻（0 = 已灭） */

/* ---------------- S-meter（BK4802 寄存器 24）采样与取值 ----------------
 * 为什么不在"解出帧的那一刻"读一次：解码完成时包已经结束，S-meter 未必还停在那个值上，
 * 单次读还有失败概率（bk4802_read_reg 内部已重试 3 次 + 总线恢复）。
 * 改成每 100ms 采一次，记录最近 1 秒内的峰值：解出帧时用"接收窗口内的峰值"，
 * 既是这包的真实强度，也不怕单次读失败。 */
#define RF_POLL_MS      BBCALL_SMETER_POLL_MS   /* 0 = 不采样（见 bbcall_cfg.h） */
#define RF_PEAK_WIN_MS  1000u
#define RF_PEAK_USE_MS  1500u
static uint8_t  s_rf_rssi, s_rf_snr;        /* 最近一次有效读数 */
static uint8_t  s_rf_valid;
static uint8_t  s_rf_pk_rssi, s_rf_pk_snr;  /* 最近 1 秒峰值 */
static uint32_t s_rf_last_ms, s_rf_pk_ms;
/* 诊断用（没有串口时用 ST-Link 的 Live Expressions 看这几个）：
 *   s_rf_ok_cnt / s_rf_fail_cnt  S-meter 采样成功/失败次数，可直接算出读失败率；
 *   s_rf_last_raw                最近一次寄存器 24 原始值（0xFFFF = 读失败）。 */
static uint16_t s_rf_ok_cnt, s_rf_fail_cnt, s_rf_last_raw = 0xFFFFu;

static void rf_smeter_poll(void)
{
  uint32_t now = HAL_GetTick();
#if RF_POLL_MS == 0
  return;                                  /* 采样已关闭：只保留"最近有效值"的兜底逻辑 */
#else
  if ((uint32_t)(now - s_rf_last_ms) < RF_POLL_MS) return;
#endif
  s_rf_last_ms = now;
  uint16_t r24 = bk4802_read_reg(24);
  s_rf_last_raw = r24;
  if ((r24 == 0xFFFFu) || ((uint8_t)(r24 & 0x00FFu) > 127u)) {   /* 读失败：只计数，保留上次的值 */
    if (s_rf_fail_cnt < 0xFFFFu) s_rf_fail_cnt++;
    return;
  }
  if (s_rf_ok_cnt < 0xFFFFu) s_rf_ok_cnt++;
  s_rf_rssi = (uint8_t)(r24 & 0x00FFu);
  s_rf_snr  = (uint8_t)((r24 & 0x3F00u) >> 8);
  s_rf_valid = 1u;
  if (((uint32_t)(now - s_rf_pk_ms) > RF_PEAK_WIN_MS) || (s_rf_rssi >= s_rf_pk_rssi)) {
    s_rf_pk_rssi = s_rf_rssi;
    s_rf_pk_snr  = s_rf_snr;
    s_rf_pk_ms   = now;
  }
}

/* 解出一帧时决定这条消息显示的 RSSI/SNR（返回 1 = 用的是"解码当刻"直读值，0 = 用了兜底）：
 *   1) 先直读一次寄存器 24（bk4802_read_reg 内部已重试 3 次 + 总线恢复）——这就是这包解出来时的读数；
 *   2) 读失败（0xFFFF / RSSI>127）退回接收窗口（1s）峰值——包刚结束，S-meter 未必还停在原值上；
 *   3) 峰值也过期就退回最近一次有效采样；一次有效采样都没有才 --。 */
static uint8_t rf_apply_for_frame(void)
{
  uint32_t now = HAL_GetTick();
  uint16_t r24 = bk4802_read_reg(24);
  s_rf_last_raw = r24;
  if ((r24 != 0xFFFFu) && ((uint8_t)(r24 & 0x00FFu) <= 127u)) {
    s_rf_rssi = (uint8_t)(r24 & 0x00FFu);
    s_rf_snr  = (uint8_t)((r24 & 0x3F00u) >> 8);
    s_rf_valid = 1u;
    if (s_rf_ok_cnt < 0xFFFFu) s_rf_ok_cnt++;
    if (((uint32_t)(now - s_rf_pk_ms) > RF_PEAK_WIN_MS) || (s_rf_rssi >= s_rf_pk_rssi)) {
      s_rf_pk_rssi = s_rf_rssi;
      s_rf_pk_snr  = s_rf_snr;
      s_rf_pk_ms   = now;
    }
    ui_set_radio_stats((int16_t)s_rf_rssi, (int16_t)s_rf_snr);
    return 1u;
  }
  if (s_rf_fail_cnt < 0xFFFFu) s_rf_fail_cnt++;
  if (s_rf_valid && ((uint32_t)(now - s_rf_pk_ms) <= RF_PEAK_USE_MS)) {
    ui_set_radio_stats((int16_t)s_rf_pk_rssi, (int16_t)s_rf_pk_snr);
    return 0u;
  }
  if (s_rf_valid) {
    ui_set_radio_stats((int16_t)s_rf_rssi, (int16_t)s_rf_snr);
    return 0u;
  }
  ui_set_radio_stats((int16_t)-32768, (int16_t)-32768);
  return 0u;
}

static void ui_backlight_wake(void)
{
  lcd_backlight(1u);
  s_bl_off_at = HAL_GetTick() + 15000u;   /* 任意按键后背光亮 15s */
}

/* 按键诊断（没有串口时用 ST-Link 的 Live Expressions 看）：
 *   s_key_up_cnt / s_key_down_cnt   ▲▼ 触发次数
 *   s_key_ok_cnt / s_key_oklong_cnt ● 短按 / 长按次数
 *   s_key_last_ms                   最近一次 ● 的按住时长（ms）——判断是否被误判成长按
 *   s_key_last_raw                  最近一次 ● 松开时读到的原始电平（1=未按） */
static uint16_t s_key_up_cnt, s_key_down_cnt, s_key_ok_cnt, s_key_oklong_cnt;
static uint16_t s_key_last_ms;
static uint8_t  s_key_last_raw = 1u;

static void ui_keys_poll(void)
{
  static const struct { GPIO_TypeDef *g; uint16_t p; } K[3] = {
    { KEY_UP_GPIO, KEY_UP_PIN },
    { KEY_DOWN_GPIO, KEY_DOWN_PIN },
    { KEY_OK_GPIO, KEY_OK_PIN },
  };
  static uint8_t  lvl[3] = { 1u, 1u, 1u };  /* 稳定电平，1 = 未按（上拉） */
  static uint8_t  raw[3] = { 1u, 1u, 1u };
  static uint8_t  cnt[3];
  static uint32_t down_ms[3], rep_ms[3], t_scan;
  uint32_t now = HAL_GetTick();
  uint8_t i;

  if ((uint32_t)(now - t_scan) < 10u) return;
  t_scan = now;

  for (i = 0u; i < 3u; i++) {
    uint8_t r = (HAL_GPIO_ReadPin(K[i].g, K[i].p) == GPIO_PIN_RESET) ? 0u : 1u;
    if (r == raw[i]) { if (cnt[i] < 3u) cnt[i]++; }
    else             { raw[i] = r; cnt[i] = 0u; }
    if (cnt[i] < 3u || r == lvl[i]) continue;
    lvl[i] = r;
    if (r == 0u) {                            /* 按下沿 */
      down_ms[i] = now;
      rep_ms[i]  = now;
      if (i < 2u) {
        if (i == 0u) { if (s_key_up_cnt   < 0xFFFFu) s_key_up_cnt++;   }
        else         { if (s_key_down_cnt < 0xFFFFu) s_key_down_cnt++; }
        ui_handle_key(i == 0u ? SIM_KEY_UP : SIM_KEY_DOWN);
        ui_backlight_wake();
      }
    } else if (i == 2u) {                     /* ● 松开沿：短按/长按互斥 */
      uint32_t held = (uint32_t)(now - down_ms[i]);
      s_key_last_ms  = (held > 0xFFFFu) ? 0xFFFFu : (uint16_t)held;
      s_key_last_raw = r;
      if (held >= 620u) { if (s_key_oklong_cnt < 0xFFFFu) s_key_oklong_cnt++; }
      else              { if (s_key_ok_cnt     < 0xFFFFu) s_key_ok_cnt++;     }
      ui_handle_key((held >= 620u) ? SIM_KEY_OK_LONG : SIM_KEY_OK);
      ui_backlight_wake();
    }
  }
  /* ▲▼ 长按自动重复 */
  for (i = 0u; i < 2u; i++) {
    if (lvl[i] == 0u && (uint32_t)(now - down_ms[i]) >= 600u &&
        (uint32_t)(now - rep_ms[i]) >= 180u) {
      rep_ms[i] = now;
      ui_handle_key(i == 0u ? SIM_KEY_UP : SIM_KEY_DOWN);
      ui_backlight_wake();
    }
  }
}
#endif /* BBCALL_LCD_ENABLED */

void bbcall_app_init(void)
{
  hw_board_pins_init();
  hw_console_init(CONSOLE_BAUD);
  hw_console_puts("\r\n[BBcall_APRS] boot\r\n");
  hw_console_puts(hw_clock_is72() ? "[CLK] HSE 72MHz\r\n" : "[CLK] HSI (check 8MHz HSE)\r\n");
  bk4802_init();                /* CE(PA0) 拉高、DIO1(PA8) 拉低、SCL/SDA 就绪 */
  bk4802_enter_rx();
  bk4802_set_rx_freq_khz(BBCALL_DEF_FREQ_KHZ);
  /* 打印实际配置：默认频率与频率字跟随 bbcall_cfg.h */
  {
    unsigned fmhz = BBCALL_DEF_FREQ_KHZ / 1000u;
    unsigned kmhz = BBCALL_DEF_FREQ_KHZ % 1000u;
    char tmp[24];
    uint8_t n = 0;
    const char *tag = "[BBcall] RX ";
    while (*tag) tmp[n++] = *tag++;
    tmp[n++] = (char)('0' + fmhz / 100u);        /* 百位，目标 <1000MHz */
    tmp[n++] = (char)('0' + (fmhz / 10u) % 10u);
    tmp[n++] = (char)('0' + fmhz % 10u);
    tmp[n++] = '.';
    tmp[n++] = (char)('0' + (kmhz / 100u) % 10u);
    tmp[n++] = (char)('0' + (kmhz / 10u) % 10u);
    tmp[n++] = (char)('0' + kmhz % 10u);
    const char *tail = " MHz\r\n";
    while (*tail) tmp[n++] = *tail++;
    for (uint8_t i = 0; i < n; i++) hw_console_putc(tmp[i]);
  }
  {
    /* 与 bk4802_set_rx_freq_khz 同一个整数算法，只用于打印（原来这里也有一份 double 版本，
     * 会把软浮点库拖进固件；64KB Flash 放不下，已合并成整数实现） */
    uint16_t want_r2 = 0x2004u;
    uint32_t value = 0u;
    bk4802_freq_word_khz(BBCALL_DEF_FREQ_KHZ, &want_r2, &value);
    hw_console_puts("[FREQ] want r2=");
    hw_console_hex16(want_r2);
    hw_console_puts(" r0=");
    hw_console_hex16((uint16_t)(value >> 16));
    hw_console_puts(" r1=");
    hw_console_hex16((uint16_t)(value & 0xFFFFu));
    hw_console_puts("\r\n");
  }
  /* BK4802 是位敲 I2C：上电第一次整组读偶尔全 0xFFFF（实测遇到过）。
   * 这时按"配置可能没写进去"处理：重新初始化 + 重新设频，再读一次。 */
  {
    uint16_t rr2 = bk4802_read_reg(2), rr0 = bk4802_read_reg(0), rr1 = bk4802_read_reg(1);
    if (rr2 == 0xFFFFu && rr0 == 0xFFFFu && rr1 == 0xFFFFu) {
      hw_console_puts("[FREQ] read all FFFF -> re-init BK4802\r\n");
      bk4802_init();
      bk4802_enter_rx();
      bk4802_set_rx_freq_khz(BBCALL_DEF_FREQ_KHZ);
      rr2 = bk4802_read_reg(2); rr0 = bk4802_read_reg(0); rr1 = bk4802_read_reg(1);
    }
    hw_console_puts("[FREQ] read r2=");
    hw_console_hex16(rr2);
    hw_console_puts(" r0=");
    hw_console_hex16(rr0);
    hw_console_puts(" r1=");
    hw_console_hex16(rr1);
    hw_console_puts("\r\n");
    if (rr2 == 0xFFFFu && rr0 == 0xFFFFu && rr1 == 0xFFFFu) {
      /* 重试后还是全 FFFF：打总线电平，区分"线被拉死"和"线好但芯片不应答" */
      uint8_t ps, pd, pa, pr8;
      bk4802_bus_probe(&ps, &pd, &pa, &pr8);
      hw_console_puts("[I2C] no response: scl="); hw_console_u8(ps);
      hw_console_puts(" sda="); hw_console_u8(pd);
      hw_console_puts(" relPA8="); hw_console_u8(pr8);
      hw_console_puts(" ack="); hw_console_u8(pa);
      hw_console_puts(" (1=high; ack=1 means chip answers)\r\n");
    }
  }

  /* 片内 RTC：优先 LSE(32.768k 晶振)，没有就退到 HSE/128(主板 8MHz 晶振)，最后 LSI；
   * 掉电（没有 VBAT 电池）后时间会丢，重新上电跑 tools/set_rtc_time.ps1 再对一次 */
  bbcall_rtc_init();
  hw_console_puts("[RTC] cfg=");
  hw_console_u8(bbcall_rtc_clk_cfg());   /* 1 = 只用 LSE（32.768k 晶振） */
  hw_console_puts(" src=");
  hw_console_puts(bbcall_rtc_clock_src_name());
  if (bbcall_rtc_clock_src() == RTC_SRC_LSE) {
    hw_console_puts(" lse=");
    if (bbcall_rtc_lse_start_ms() == 0xFFFFu) hw_console_puts("--");   /* 沿用已有备份域：本次没测 */
    else { hw_console_u16(bbcall_rtc_lse_start_ms()); hw_console_puts("ms"); }
  } else if (bbcall_rtc_clock_src() == RTC_SRC_NONE) {
    hw_console_puts(" ERR: no RTC clock source (cfg=1 -> check 32.768k crystal / load caps)");
  }
  rtc_print_trim();   /* 分频比 = 实际走时速率，配指南见 bbcall_cfg.h 的 BBCALL_RTC_TRIM_PPM */
#if BBCALL_RTC_SEED_BUILD_TIME
  /* 没焊串口也能有真实时间：RTC 还没对过时用编译时间戳兜底（= CubeIDE 点 Build 的那一刻）。
   * 接上串口后可随时用 tools/set_rtc_time.ps1 覆盖成精确时间。 */
  if (!bbcall_rtc_valid()) {
    rtc_dt_t bdt;
    if (bbcall_rtc_build_time(&bdt) && bbcall_rtc_seed_build_time()) {
      char bbuf[20];
      rtc_format_dt(&bdt, bbuf, (uint8_t)sizeof(bbuf));
      hw_console_puts(" seeded=");
      hw_console_puts(bbuf);
    }
  }
#endif
  if (bbcall_rtc_valid()) {
    rtc_dt_t rdt;
    if (bbcall_rtc_get(&rdt)) {
      char rbuf[20];
      rtc_format_dt(&rdt, rbuf, (uint8_t)sizeof(rbuf));
      hw_console_puts(" time=");
      hw_console_puts(rbuf);
      hw_console_puts(" wday=");
      hw_console_u8(rdt.wday);
    }
  } else if (bbcall_rtc_clock_src() != RTC_SRC_NONE) {
    hw_console_puts(" no-time (send TIME=YYYY-MM-DD HH:MM:SS)");
  }
  hw_console_puts("\r\n");

#if BBCALL_LCD_ENABLED
  /* 三态 UI（design.md v2.0）：ui_init 内含 lcd_init()，模拟器与真机同一份代码 */
  ui_init();
  ui_set_mycall(BBCALL_MYCALL);
  ui_set_rx_freq_khz(BBCALL_DEF_FREQ_KHZ);
#if BBCALL_WALLCLOCK_ENABLE
  /* 锁屏页默认墙钟（bbcall_cfg.h）：周W M/D + HH:MM，开机即从这一刻走 */
  ui_set_wallclock(BBCALL_WALLCLOCK_WDAY, BBCALL_WALLCLOCK_MON, BBCALL_WALLCLOCK_DAY);
  ui_set_clock_ms(BBCALL_WALLCLOCK_BASE_MS);
  rtc_apply_to_ui();   /* 已对过时：用 RTC 真实时间覆盖默认值 */
#endif
  ui_show(UI_SCREEN_IDLE);
  lcd_backlight(1u);
  s_bl_off_at = HAL_GetTick() + 15000u;
  hw_console_puts("[LCD] UI v2.0 three-state (design.md), keys: UP/DOWN/OK, long-press 620ms\r\n");
#else
  hw_console_puts("[LCD] disabled (not soldered yet), console mode\r\n");
#endif

  modem_init();
  hw_timers_init();             /* TIM3 ADC 采样，中断喂 modem */
  hw_watchdog_init(2000u);      /* 2 秒独立看门狗 */
#if BBCALL_SW_SQUELCH
  bk4802_set_rx_audio_mute(1);  /* 上电静音，检测到有效信号再放开 */
#endif
  HAL_GPIO_WritePin(LED_GPIO, LED_PIN, GPIO_PIN_SET);
}

void bbcall_app_loop(void)
{
  static uint32_t t_beat = 0;
  static uint32_t t_smet = 0;
#if BBCALL_SW_SQUELCH
  static uint32_t t_sq = 0;
#endif
  static uint8_t audio_muted = BBCALL_SW_SQUELCH ? 1u : 0u;
  static ax25_frame_t fr;
  static uint16_t dup_hash[8];
  static char dup_name[8][7];
  static uint32_t dup_time[8];
  static uint8_t dup_pos = 0;
  static uint32_t rx_count = 0, dup_count = 0;
  static char uniq[16][7];
  static uint8_t n_uniq = 0;
  static uint32_t last_frame_tick = 0;

  console_poll();   /* 串口命令：TIME=... / TIME?（PC 对时） */

  if (modem_get_frame(&fr)) {
    if (modem_frame_was_fixed()) hw_console_puts("[FIX] ");
    else if (modem_frame_was_repeat()) hw_console_puts("[REP] ");
    hw_console_puts("[T=");
    hw_console_u32(HAL_GetTick());
    hw_console_puts("ms] ");
    /* 重复包抑制：同一帧 60s 内只打印一次，避免串口刷屏 */
    uint16_t fh = 0;
    for (uint16_t i = 0; i < fr.len; i++) fh = (uint16_t)((fh << 5) ^ (fh >> 2) ^ fr.frame[i]);
    char fname[7];
    for (uint8_t i = 0; i < 6u; i++) fname[i] = (char)((fr.frame[7u + i] >> 1) & 0x7Fu);
    fname[6] = '\0';
    uint32_t now_ms = HAL_GetTick();
    uint8_t is_dup = 0;
    for (uint8_t i = 0; i < 8u; i++) {
      if (dup_name[i][0] && dup_hash[i] == fh && strcmp(dup_name[i], fname) == 0 &&
          (now_ms - dup_time[i]) < 60000u) { is_dup = 1; break; }
    }
    if (is_dup && !modem_frame_was_repeat()) {
      dup_count++;
      hw_console_puts("[DUP] src=");
      hw_console_puts(fname);
      hw_console_puts("\r\n");
#if BBCALL_LCD_ENABLED
      /* 重复包不再丢弃：仍交给 UI，把这条消息的时间戳刷新成现在（用户 2026-09-18 要求）。
       * 不打印 [RAW]/[FRAME]、不加 rx_count，避免日志与统计被重复包淹没。 */
      rf_apply_for_frame();
      if (ui_feed_ax25(fr.frame, fr.len, ui_clock_ms(),
                       modem_frame_was_fixed() ? 1u : 0u,
                       modem_frame_was_repeat() ? 1u : 0u)) {
        ui_backlight_wake();
      }
#endif
    } else {
      rx_count++;
      last_frame_tick = now_ms;
      dup_hash[dup_pos] = fh;
      for (uint8_t i = 0; i < 6u; i++) dup_name[dup_pos][i] = fname[i];
      dup_name[dup_pos][6] = '\0';
      dup_time[dup_pos] = now_ms;
      dup_pos = (uint8_t)((dup_pos + 1u) & 7u);
      uint8_t found = 0;
      for (uint8_t i = 0; i < n_uniq; i++) if (strcmp(uniq[i], fname) == 0) { found = 1; break; }
      if (!found && n_uniq < 16u) {
        for (uint8_t i = 0; i < 6u; i++) uniq[n_uniq][i] = fname[i];
        uniq[n_uniq][6] = '\0';
        n_uniq++;
      }
#if BBCALL_RAW_LOG
    hw_console_puts("\r\n[RAW] len=");
    hw_console_u16(fr.len);
    hw_console_puts(" hex=");
    for (uint8_t i = 0; i < fr.len; i++) hw_console_hex8(fr.frame[i]);
    hw_console_puts("\r\n");
#endif
    static ax25_decoded_t d;
    if (ax25_decode(fr.frame, fr.len, &d)) {
#if BBCALL_LCD_ENABLED
      /* 解码当刻取这条消息的 RSSI/SNR：先直读寄存器 24，读失败才用采样窗口兜底（见函数说明） */
      uint8_t rf_fresh = rf_apply_for_frame();
      if (ui_feed_ax25(fr.frame, fr.len, ui_clock_ms(),   /* 与锁屏同一基准 */
                       modem_frame_was_fixed() ? 1u : 0u,
                       modem_frame_was_repeat() ? 1u : 0u)) {
        /* 真的入箱了（不是重复包/ackNNN）：点亮背光，否则背光超时后看不到这条新消息 */
        ui_backlight_wake();
      }
#endif
      hw_console_puts("\r\n[FRAME] src=");
      hw_console_puts(d.src);
      if (d.src_ssid != 0u) { hw_console_putc('-'); hw_console_u16((uint16_t)d.src_ssid); }
      hw_console_puts(" dest=");
      hw_console_puts(d.dest);
      hw_console_puts(" path=");
      if (d.npath == 0u) hw_console_puts("(none)");
      for (uint8_t i = 0; i < d.npath; i++) {
        if (i) hw_console_putc(',');
        hw_console_puts((const char *)d.path[i]);
        hw_console_putc('-');
        hw_console_u8(d.path_ssid[i]);
        if (d.path_h[i]) hw_console_putc('*');
      }
      hw_console_puts(" ctrl=");
      hw_console_u8(d.control);
      hw_console_puts(" info=");
      for (uint8_t i = 0; i < d.info_len; i++) hw_console_putc((char)d.info[i]);
#if BBCALL_LCD_ENABLED
      /* 本帧记下的 RSSI/SNR（芯片原始读数，非 dBm）：没有有效采样时打 -- */
      if (s_rf_valid) {
        hw_console_puts(" RSSI="); hw_console_u8(s_rf_rssi);
        hw_console_puts(" SNR=");  hw_console_u8(s_rf_snr);
        hw_console_puts(rf_fresh ? " (decode-now)" : " (peak-fallback)");
      } else {
        hw_console_puts(" RSSI=-- SNR=--");
      }
#endif
      hw_console_puts("\r\n");
      /* Mic-E 的目标呼号包含位置模糊度空格，必须用原始地址字节（保留空格） */
      char mice_dest[7];
      for (uint8_t i = 0; i < 6u; i++) {
        uint8_t c = (uint8_t)((fr.frame[i] >> 1) & 0x7Fu);
        mice_dest[i] = (char)((c == 0u) ? ' ' : (char)c);
      }
      mice_dest[6] = '\0';
      static aprs_mice_t mi;
      if (aprs_parse_mice(mice_dest, d.info, d.info_len, &mi)) {
        hw_console_puts(" [MICE] lat=");
        hw_console_puts(mi.lat);
        hw_console_puts(" lon=");
        hw_console_puts(mi.lon);
        hw_console_puts(" spd=");
        hw_console_u16(mi.speed_kmh);
        hw_console_puts("km/h crs=");
        hw_console_u16(mi.course);
        hw_console_puts(" msg=");
        hw_console_puts(mi.mtype);
        hw_console_puts(" comment=");
        hw_console_puts(mi.comment);
        hw_console_puts("\r\n");
      }
      static aprs_position_t pos;
      if (aprs_parse_position(d.info, d.info_len, &pos)) {
        hw_console_puts(" [POS] lat=");
        hw_console_puts(pos.lat);
        hw_console_puts(" lon=");
        hw_console_puts(pos.lon);
        if (pos.comment[0]) { hw_console_puts(" comment="); hw_console_puts(pos.comment); }
        hw_console_puts("\r\n");
      }
      static aprs_message_t m;
      if (aprs_parse_message(d.info, d.info_len, &m)) {
        hw_console_puts(" msg=");
        for (uint8_t i = 0; i < m.body_len; i++) hw_console_putc((char)m.body[i]);
        hw_console_puts("\r\n");
      }
    }
    }
  }

  if (HAL_GetTick() - t_beat >= 500u) {
    t_beat = HAL_GetTick();
    static uint16_t prev_mk = 0, prev_sp = 0, prev_ot = 0;
    uint16_t mk, sp, ot;
    modem_get_stats(&mk, &sp, &ot);
    uint16_t amin, amax;
    modem_get_adc_range(&amin, &amax);
    uint16_t dmk = (uint16_t)(mk - prev_mk);
    uint16_t dsp = (uint16_t)(sp - prev_sp);
    uint16_t dot = (uint16_t)(ot - prev_ot);
    prev_mk = mk; prev_sp = sp; prev_ot = ot;
    HAL_GPIO_TogglePin(LED_GPIO, LED_PIN);
    hw_console_puts("S=");
    hw_console_u8(bk4802_get_smeter());
    hw_console_puts(" MK=");
    hw_console_u16(dmk);
    hw_console_puts(" SP=");
    hw_console_u16(dsp);
    hw_console_puts(" OT=");
    hw_console_u16(dot);
    hw_console_puts(" A0=");
    hw_console_u16(amin);
    hw_console_puts(" A1=");
    hw_console_u16(amax);
    hw_console_puts(" M=");
    hw_console_u8(audio_muted);
    hw_console_puts("\r\n");
  }

  if (HAL_GetTick() - t_smet >= 2000u) {
    t_smet = HAL_GetTick();
    /* 诊断：打印读回的寄存器原始值，判断 I2C 读与 S-meter 映射 */
    uint16_t r24 = bk4802_read_reg(24);
    uint8_t rssi_now = (uint8_t)(r24 & 0x00FFu);
    /* 自适应中频增益（带迟滞）：弱信号提高增益、强信号降低。
     * 强信号下 I2C 读可能失效（0xFFFF / RSSI>127），此时不调整；
     * 刚收到帧的 1 秒内也不切换增益，避免包中途改变增益影响解码。 */
    uint8_t rssi_ok = (r24 != 0xFFFFu) && (rssi_now <= 127u);
    if (rssi_ok) {                     /* 与 100ms 快采共用同一份缓存 */
      s_rf_rssi = rssi_now;
      s_rf_snr  = (uint8_t)((r24 & 0x3F00u) >> 8);
      s_rf_valid = 1u;
    }
    uint8_t can_adjust = rssi_ok && ((HAL_GetTick() - last_frame_tick) > 1000u);
    static uint8_t if_code = BK4802_IF_GAIN_CODE;
    uint8_t new_code = if_code;
#if BBCALL_IF_AGC
    /* 范围 + 双阈值（带滞回）：RSSI 太低就升档、太高就降档，档位夹在 [MIN, MAX]。
     * 换档范围与阈值都在 bbcall_cfg.h，改那里即可（含"固定增益"开关）。 */
    if (can_adjust) {
      if ((rssi_now < BK4802_AGC_UP_RSSI) && (if_code < BK4802_IF_GAIN_MAX))
        new_code = (uint8_t)(if_code + 1u);
      else if ((rssi_now > BK4802_AGC_DN_RSSI) && (if_code > BK4802_IF_GAIN_MIN))
        new_code = (uint8_t)(if_code - 1u);
    }
#else
    (void)can_adjust;   /* 固定增益：不做任何自动调整 */
#endif
    if (new_code != if_code) { if_code = new_code; bk4802_set_if_gain_code(if_code); }
    if (r24 == 0xFFFFu) {   /* 读失败：顺带打总线电平，便于抓"线被拉死/芯片不应答" */
      uint8_t ps, pd, pa, pr8;
      bk4802_bus_probe(&ps, &pd, &pa, &pr8);
      hw_console_puts("[I2C] scl="); hw_console_u8(ps);
      hw_console_puts(" sda="); hw_console_u8(pd);
      hw_console_puts(" relPA8="); hw_console_u8(pr8);
      hw_console_puts(" ack="); hw_console_u8(pa);
      hw_console_puts("\r\n");
    }
    hw_console_puts("R19=");
    hw_console_u16(bk4802_read_reg(19));
    hw_console_puts(" RSSI=");
    hw_console_u16(r24 & 0x00FFu);
    hw_console_puts(" SNR=");
    hw_console_u16((r24 & 0x3F00u) >> 8);
    hw_console_puts(" G=");
    hw_console_u8(if_code);
    hw_console_puts(" RX="); hw_console_u16((uint16_t)rx_count);
    hw_console_puts(" U="); hw_console_u8(n_uniq);
    hw_console_puts(" DUP="); hw_console_u16((uint16_t)dup_count);
    hw_console_puts(" FIX="); hw_console_u16(modem_get_fix_count());
    hw_console_puts(" FIX2="); hw_console_u16(modem_get_fix2_count());
    hw_console_puts(" REP="); hw_console_u16(modem_get_rep_count());
    hw_console_puts(" I2CE="); hw_console_u16(bk4802_i2c_error_count());
    hw_console_puts(" AFC=");
    hw_console_u16(bk4802_read_reg(25) & 0x00FFu);
    hw_console_puts(" EXN=");
    hw_console_u16(bk4802_read_reg(26) & 0x1FFFu);
    hw_console_puts(" R22=");
    hw_console_hex16(bk4802_read_reg(22));
    hw_console_puts(" R23=");
    hw_console_hex16(bk4802_read_reg(23));
    hw_console_puts(" ID=");
    hw_console_u16(bk4802_read_reg(27));
    hw_console_puts(" T=");
    hw_console_u8(modem_tone_now());
    hw_console_puts(" P=");
    hw_console_u16(modem_last_period());
    hw_console_puts("\r\n");
  }

#if BBCALL_LCD_ENABLED
  rf_smeter_poll();   /* 每 100ms 采一次 S-meter，供解码时取接收窗口峰值 */
  /* 三态 UI：推进设备时钟（态 1/2 冒号闪烁 0.5s 触发重绘）+ 扫描按键 + 背光超时熄灭 */
  {
    static uint32_t t_ui_last = 0;
    uint32_t t_ui_now = HAL_GetTick();
    ui_tick(t_ui_now - t_ui_last);
    t_ui_last = t_ui_now;
  }
  /* 每秒跟 RTC 查一次：跨零点、PC 刚对过时，画面时钟与日期立即跟上（平时不动，避免抖动） */
  {
    static uint32_t t_rtc_sync = 0;
    if ((uint32_t)(HAL_GetTick() - t_rtc_sync) >= 1000u) {
      t_rtc_sync = HAL_GetTick();
      if (bbcall_rtc_valid()) {
        uint32_t dm = bbcall_rtc_day_ms();
        uint32_t cur = ui_clock_ms() % 86400000u;
        int32_t dd = (int32_t)(dm - cur);
        if (dd < 0) dd = -dd;
        if (dd > 1200) rtc_apply_to_ui();
      }
    }
  }
  ui_keys_poll();
  if (s_bl_off_at && (int32_t)(HAL_GetTick() - s_bl_off_at) >= 0) {
    lcd_backlight(0u);
    s_bl_off_at = 0u;
  }
#endif

  hw_watchdog_feed();
#if BBCALL_SW_SQUELCH
  /* 软件静噪：RSSI>=110 就放开音频。
   * 不查 EXN：近距离强信号会干扰 I2C，EXN 读错会导致一直不放行。 */
  if (HAL_GetTick() - t_sq >= 50u) {
    t_sq = HAL_GetTick();
    uint16_t r24 = bk4802_read_reg(24);
    uint8_t rssi = (uint8_t)(r24 & 0x00FFu);
    uint8_t want_mute = 1u;
    if (rssi >= 110u) want_mute = 0u;
    if (want_mute != audio_muted) {
      audio_muted = want_mute;
      bk4802_set_rx_audio_mute(want_mute);
    }
  }
#endif

}
