/* BBcall 应用：初始化 + 主循环（无硬件串口；状态经 LED/LCD）。 */
#include "main.h"
#include "bbcall_cfg.h"
#include "bbcall_hw.h"
#include "bbcall_app.h"
#include "bk4802.h"
#include "lcd_st7567.h"
#include "modem.h"
#include "ax25.h"
#include "aprs.h"

#ifndef BBCALL_LCD_ENABLED
#define BBCALL_LCD_ENABLED 0u   /* LCD 未焊接前：纯串口调试 */
#endif

#if BBCALL_LCD_ENABLED
static void lcd_u8(uint8_t x, uint8_t y, uint8_t v)
{
  lcd_draw_char8x16(x, y, (uint8_t)('0' + v / 100), 1);
  lcd_draw_char8x16(x + 8, y, (uint8_t)('0' + (v / 10) % 10), 1);
  lcd_draw_char8x16(x + 16, y, (uint8_t)('0' + v % 10), 1);
}
#endif

void bbcall_app_init(void)
{
  hw_board_pins_init();
  hw_console_init(CONSOLE_BAUD);
  hw_console_puts("\r\n[BBcall_APRS] boot\r\n");
  bk4802_init();                /* CE(PA0) 拉高、DIO1(PA8) 拉低、SCL/SDA 就绪 */
  bk4802_enter_rx();
  bk4802_set_rx_freq_mhz(BBCALL_DEF_FREQ_MHZ);
  /* 打印实际配置：默认频率与频率字跟随 bbcall_cfg.h */
  {
    unsigned fmhz = (unsigned)BBCALL_DEF_FREQ_MHZ;
    unsigned kmhz = (unsigned)((BBCALL_DEF_FREQ_MHZ - (double)fmhz) * 1000.0 + 0.5);
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
    /* 与 bk4802_set_rx_freq_mhz 相同的 24bit 频率字计算，仅用于打印 */
    const double xtal_mhz = 21.25;
    const double if_mhz   = 0.137;
    double mhz = BBCALL_DEF_FREQ_MHZ;
    uint32_t ndiv = 12;
    uint16_t want_r2 = 0x2004u;
    if (mhz >= 384.0f && mhz <= 512.0f) { ndiv = 4;  want_r2 = 0x0002u; }
    else if (mhz >= 128.0f && mhz <= 170.0f) { ndiv = 12; want_r2 = 0x2004u; }
    double lo = (double)mhz - if_mhz;
    uint32_t value = (uint32_t)(lo * (double)ndiv * 16777216.0 / xtal_mhz + 0.5);
    hw_console_puts("[FREQ] want r2=");
    hw_console_hex16(want_r2);
    hw_console_puts(" r0=");
    hw_console_hex16((uint16_t)(value >> 16));
    hw_console_puts(" r1=");
    hw_console_hex16((uint16_t)(value & 0xFFFFu));
    hw_console_puts("\r\n");
  }
  hw_console_puts("[FREQ] read r2=");
  hw_console_hex16(bk4802_read_reg(2));
  hw_console_puts(" r0=");
  hw_console_hex16(bk4802_read_reg(0));
  hw_console_puts(" r1=");
  hw_console_hex16(bk4802_read_reg(1));
  hw_console_puts("\r\n");

#if BBCALL_LCD_ENABLED
  lcd_init();
  lcd_clear(0);
  lcd_draw_string8x16(0, 0, "BBCALL APRS RX", 1);
  lcd_draw_string8x16(0, 16, "144.640 MHz", 1);
  lcd_flush();
#else
  hw_console_puts("[LCD] disabled (not soldered yet), console mode\r\n");
#endif

  modem_init();
  hw_timers_init();             /* TIM2 捕获 + TIM3 采样，中断喂 modem */
#if BBCALL_SW_SQUELCH
  bk4802_set_rx_audio_mute(1);  /* 上电静音，检测到有效信号再放开 */
#endif
  HAL_GPIO_WritePin(LED_GPIO, LED_PIN, GPIO_PIN_SET);
}

#if BBCALL_LCD_ENABLED
static void show_message_line(uint8_t y, const uint8_t *s, uint8_t max)
{
  char tmp[17];
  uint8_t n = 0;
  while (n < max && n < 16u && s[n]) { tmp[n] = (char)s[n]; n++; }
  tmp[n] = 0;
  lcd_draw_string8x16(0, y, tmp, 1);
}
#endif

void bbcall_app_loop(void)
{
  static uint32_t t_beat = 0;
  static uint32_t t_smet = 0;
#if BBCALL_SW_SQUELCH
  static uint32_t t_sq = 0;
#endif
  static uint8_t audio_muted = BBCALL_SW_SQUELCH ? 1u : 0u;
  ax25_frame_t fr;

  if (modem_get_frame(&fr)) {
    hw_console_puts("\r\n[RAW] len=");
    hw_console_u16(fr.len);
    hw_console_puts(" hex=");
    for (uint8_t i = 0; i < fr.len; i++) hw_console_hex8(fr.frame[i]);
    hw_console_puts("\r\n");
    ax25_decoded_t d;
    if (ax25_decode(fr.frame, fr.len, &d)) {
      hw_console_puts("\r\n[FRAME] src=");
      hw_console_puts(d.src);
      hw_console_puts(" ctrl=");
      hw_console_u8(d.control);
      hw_console_puts(" info=");
      for (uint8_t i = 0; i < d.info_len; i++) hw_console_putc((char)d.info[i]);
      hw_console_puts("\r\n");
      aprs_message_t m;
      if (aprs_parse_message(d.info, d.info_len, &m)) {
        hw_console_puts(" msg=");
        for (uint8_t i = 0; i < m.body_len; i++) hw_console_putc((char)m.body[i]);
        hw_console_puts("\r\n");
#if BBCALL_LCD_ENABLED
        lcd_clear(0);
        lcd_draw_string8x16(0, 0, "FROM ", 1);
        lcd_draw_string8x16(40, 0, d.src, 1);
        lcd_draw_string8x16(0, 16, "--------------------", 1);
        show_message_line(24, m.body, m.body_len);
        show_message_line(40, m.body + 16, m.body_len > 16 ? (uint8_t)(m.body_len - 16) : 0);
        lcd_flush();
#endif
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
    hw_console_puts("R19=");
    hw_console_u16(bk4802_read_reg(19));
    hw_console_puts(" RSSI=");
    hw_console_u16(r24 & 0x00FFu);
    hw_console_puts(" SNR=");
    hw_console_u16((r24 & 0x3F00u) >> 8);
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
#if BBCALL_LCD_ENABLED
    lcd_draw_string8x16(0, 48, "S=", 1);
    lcd_u8(16, 48, bk4802_get_smeter());
    lcd_flush();
#endif
  }

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
