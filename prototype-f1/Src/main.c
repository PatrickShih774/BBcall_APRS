/*
 * BBcall_APRS - 应用主程序（Phase 1/2 bring-up 参考）
 * 说明：此文件用 STM32CubeF1 HAL 编写，请在 STM32CubeIDE/Keil 生成的工程里
 *       编译。需按 docs/wiring.md 使能 USART1、TIM2 CH2(PA1) 输入捕获、TIM3 采样定时器。
 * 目标：设频 144.640MHz、读 S-meter、USART 打印；modem/LCD 预留接入。
 */
#include "stm32f1xx_hal.h"
#include "bbcall_config.h"
#include "bk4802.h"
#include "lcd_st7567.h"
#include "modem.h"
#include "ax25.h"
#include "aprs.h"

#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef huart1;
static TIM_HandleTypeDef  htim2;   /* PA1 输入捕获（零交叉周期） */
static TIM_HandleTypeDef  htim3;   /* 采样定时器（约 9600Hz） */

static uint32_t s_last_ccr = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_Init(void);
static void MX_TIM2_IC_Init(void);
static void MX_TIM3_Init(void);

static void console_send(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
}

static void console_u8(uint8_t v)
{
    char b[4] = {0};
    b[0] = v ? (char)('0' + v / 100) : '0';
    b[1] = (char)('0' + (v / 10) % 10);
    b[2] = (char)('0' + v % 10);
    console_send(b);
}

/* 输入捕获回调：周期 -> modem */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        uint32_t ccr = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_2);
        uint32_t period_us = ccr - s_last_ccr;
        if ((int32_t)period_us <= 0) period_us += 0xFFFFFFFFUL; /* 处理回绕（未精确处理溢出位） */
        s_last_ccr = ccr;
        if (period_us > 0 && period_us < 5000) modem_on_capture_period((uint16_t)period_us);
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) modem_sample();
}

#ifndef BBCALL_USE_CUBEMX_IT
/* 若依赖 CubeIDE 生成的 stm32f1xx_it.c，请定义 BBCALL_USE_CUBEMX_IT=1 并去掉下方两个函数 */
void TIM2_IRQHandler(void){ HAL_TIM_IRQHandler(&htim2); }
void TIM3_IRQHandler(void){ HAL_TIM_IRQHandler(&htim3); }
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_Init();
    MX_TIM2_IC_Init();
    MX_TIM3_Init();

    bk4802_init();
    lcd_init();
    modem_init();

    bk4802_set_rx_freq_mhz(BBCALL_DEF_FREQ_MHZ);
    bk4802_enter_rx();

    console_send("\r\nBBcall_APRS RX @ ");
    char f[24];
    snprintf(f, sizeof(f), "%.3f MHz\r\n", (double)BBCALL_DEF_FREQ_MHZ);
    console_send(f);

    lcd_clear(0);
    lcd_draw_string8x16(0, 0, "BBCALL APRS RX", 1);
    lcd_flush();

    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2);

    uint32_t last = HAL_GetTick();
    for (;;) {
        if (HAL_GetTick() - last > 500) {
            last = HAL_GetTick();
            uint8_t s = bk4802_get_smeter();
            console_send("S=");
            console_u8(s);
            console_send("\r\n");
            HAL_GPIO_TogglePin(LED_GPIO, LED_PIN);
        }
        /* 每 833us 采样由 TIM3 中断驱动，modem_sample -> ax25 在这里收帧 */
        /* 帧处理在中断里通过缓冲区/标志交给这里（Phase 3 待接） */
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef o = {0};
    RCC_ClkInitTypeDef c = {0};
    o.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    o.HSEState = RCC_HSE_ON;
    o.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    o.PLL.PLLState = RCC_PLL_ON;
    o.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    o.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&o) != HAL_OK) Error_Handler();
    c.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1;
    c.APB1CLKDivider = RCC_HCLK_DIV2;
    c.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&c, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* LED / 蜂鸣 / 振动 (GPIOA6/7, GPIOB15) */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = LED_PIN;    HAL_GPIO_Init(LED_GPIO, &g);

    /* 按键（上/下/确认） */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin = KEY_UP_PIN | KEY_DOWN_PIN | KEY_OK_PIN;
    HAL_GPIO_Init(KEY_UP_GPIO, &g);
    if (KEY_DOWN_GPIO != KEY_UP_GPIO) HAL_GPIO_Init(KEY_DOWN_GPIO, &g);
    if (KEY_OK_GPIO != KEY_UP_GPIO)   HAL_GPIO_Init(KEY_OK_GPIO, &g);

    /* BK4802 I2C 引脚（PA4/PA5）已在 bk4802_init 里配，这里仅使能时钟 */
}

static void MX_USART1_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_USART1_CLK_ENABLE();
    g.Pin = GPIO_PIN_9;  g.Mode = GPIO_MODE_AF_PP; g.Speed = GPIO_SPEED_FREQ_HIGH; HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_10; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOA, &g);
    huart1.Instance = USART1;
    huart1.Init.BaudRate = CONSOLE_BAUD;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

/* TIM2 CH2 输入捕获：PA1 (AF1), 1MHz 计数(1us) */
static void MX_TIM2_IC_Init(void)
{
    GPIO_InitTypeDef g = {0};
    TIM_IC_InitTypeDef sIC = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();
    g.Pin = GPIO_PIN_1;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72 - 1;   /* 72MHz/72 = 1MHz -> 计数值=us */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFF;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_IC_Init(&htim2);
    sIC.ICPolarity = TIM_ICPOLARITY_RISING;
    sIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sIC.ICPrescaler = TIM_ICPSC_DIV1;
    sIC.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&htim2, &sIC, TIM_CHANNEL_2);
}

/* TIM3 采样：72MHz/7500 ≈ 9600Hz */
static void MX_TIM3_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 7500 - 1;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim3);
    HAL_TIM_Base_Start_IT(&htim3);
}

void Error_Handler(void)
{
    while (1) { }
}
