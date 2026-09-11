#ifndef SIM_HAL_H
#define SIM_HAL_H
#include <stdint.h>

/* 最小 HAL/GPIO 桩：让 lcd_st7567.c 能在 PC 上编译 */
typedef struct { uint32_t dummy; } GPIO_TypeDef;
extern GPIO_TypeDef *const GPIOA;
extern GPIO_TypeDef *const GPIOB;

#define GPIO_PIN_0  (1u << 0)
#define GPIO_PIN_1  (1u << 1)
#define GPIO_PIN_2  (1u << 2)
#define GPIO_PIN_3  (1u << 3)
#define GPIO_PIN_4  (1u << 4)
#define GPIO_PIN_5  (1u << 5)
#define GPIO_PIN_6  (1u << 6)
#define GPIO_PIN_7  (1u << 7)
#define GPIO_PIN_8  (1u << 8)
#define GPIO_PIN_9  (1u << 9)
#define GPIO_PIN_10 (1u << 10)
#define GPIO_PIN_11 (1u << 11)
#define GPIO_PIN_12 (1u << 12)
#define GPIO_PIN_13 (1u << 13)
#define GPIO_PIN_14 (1u << 14)
#define GPIO_PIN_15 (1u << 15)

#define GPIO_PIN_SET   1u
#define GPIO_PIN_RESET 0u
#define GPIO_MODE_OUTPUT_PP 1u
#define GPIO_MODE_INPUT     0u
#define GPIO_NOPULL         0u
#define GPIO_PULLUP         1u
#define GPIO_SPEED_FREQ_HIGH 3u

typedef struct {
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
} GPIO_InitTypeDef;

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t state);
void HAL_Delay(uint32_t ms);
void hw_delay_us(uint32_t us);

#define __HAL_RCC_GPIOB_CLK_ENABLE() do { } while (0)
#endif