#ifndef BBCALL_HW_H
#define BBCALL_HW_H
#include <stdint.h>
/* 尝试配置 HSE 8MHz * 9 = 72MHz；失败自动保持 HSI（调用后可用 hw_clock_is72() 查询） */
void hw_clock_try_72mhz(void);
uint8_t hw_clock_is72(void);
/* 微秒延时（DWT 周期计数，需 hw_delay_init 后可用） */
void hw_delay_init(void);
void hw_delay_us(uint32_t us);
/* 板级通用 GPIO（LED/蜂鸣/振动/按键） */
void hw_board_pins_init(void);
/* 寄存器级 USART3 控制台（PB10=TX, PB11=RX） */
void hw_console_init(uint32_t baud);
void hw_console_putc(char c);
void hw_console_puts(const char *s);
void hw_console_u8(uint8_t v);
void hw_console_u16(uint16_t v);
void hw_console_u32(uint32_t v);
void hw_console_hex8(uint8_t v);
void hw_console_hex16(uint16_t v);
/* 寄存器级 TIM3 9600Hz ADC 采样（中断回调进 modem） */
void hw_timers_init(void);
/* 独立看门狗（IWDG，LSI~40kHz）：ms 超时；调试暂停时冻结 */
void hw_watchdog_init(uint32_t ms);
void hw_watchdog_feed(void);
void TIM3_IRQHandler(void);
/* TIM3 ISR 负载统计：最近/最大耗时(us)、平均耗时 x100(us)、调用次数。预算 104us。 */
void hw_isr_stats(uint16_t *last_us, uint16_t *max_us, uint32_t *avg_x100, uint32_t *cnt);
#endif
