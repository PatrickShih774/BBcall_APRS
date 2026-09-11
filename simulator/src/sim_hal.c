#include "sim_hal.h"
#include <SDL.h>

static GPIO_TypeDef s_gpio_a;
static GPIO_TypeDef s_gpio_b;
GPIO_TypeDef *const GPIOA = &s_gpio_a;
GPIO_TypeDef *const GPIOB = &s_gpio_b;

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init) { (void)port; (void)init; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t state) { (void)port; (void)pin; (void)state; }
void HAL_Delay(uint32_t ms) { SDL_Delay(ms); }
void hw_delay_us(uint32_t us) { (void)us; }