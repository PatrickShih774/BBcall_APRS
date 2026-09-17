/*
 * BBcall_APRS 板级驱动（STM32F103C8Tx, 寄存器级 TIM/USART, HAL GPIO/RCC）
 * 说明：CubeIDE 工程未启用 HAL_TIM/HAL_UART 且无对应 .c，故此处直接用寄存器。
 */
#include "main.h"
#include "bbcall_cfg.h"
#include "bbcall_hw.h"
#include "modem.h"

static uint8_t s_clock72 = 0;

/* ---------------- 时钟 ---------------- */
void hw_clock_try_72mhz(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  s_clock72 = 0;
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) return;   /* 无 HSE 则保持 HSI */
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) == HAL_OK) s_clock72 = 1;
}

uint8_t hw_clock_is72(void)
{
  return s_clock72;
}

/* ---------------- 微秒延时（DWT） ---------------- */
void hw_delay_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void hw_delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = (SystemCoreClock / 1000000u) * us;
  while ((DWT->CYCCNT - start) < ticks) { }
}

/* ---------------- GPIO ---------------- */
void hw_board_pins_init(void)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* LED/蜂鸣/振动 */
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = LED_PIN;
  HAL_GPIO_Init(LED_GPIO, &g);
  HAL_GPIO_WritePin(LED_GPIO, LED_PIN, GPIO_PIN_RESET);
  g.Pin = BUZZ_PIN;
  HAL_GPIO_Init(BUZZ_GPIO, &g);
  g.Pin = VIB_PIN;
  HAL_GPIO_Init(VIB_GPIO, &g);

  /* LCD 背光（先低，lcd_init 再点亮） */
  g.Pin = LCD_BL_PIN;
  HAL_GPIO_Init(LCD_BL_GPIO, &g);
  HAL_GPIO_WritePin(LCD_BL_GPIO, LCD_BL_PIN, GPIO_PIN_RESET);

  /* 按键（上拉输入） */
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  g.Pin = KEY_UP_PIN;
  HAL_GPIO_Init(KEY_UP_GPIO, &g);
  g.Pin = KEY_DOWN_PIN;
  HAL_GPIO_Init(KEY_DOWN_GPIO, &g);
  g.Pin = KEY_OK_PIN;
  HAL_GPIO_Init(KEY_OK_GPIO, &g);

  /* 对讲机侧 PWR/PTT（MM-Radio：PA2/PA4，EXTI 上升沿，无内部上下拉） */
  g.Pull = GPIO_NOPULL;
  g.Pin = KEY_PWR_PIN;
  HAL_GPIO_Init(KEY_PWR_GPIO, &g);
  g.Pin = KEY_PTT_PIN;
  HAL_GPIO_Init(KEY_PTT_GPIO, &g);
}

/* ---------------- USART3 控制台（寄存器级, TX=PB10 RX=PB11） ---------------- */
/* ---------------- USART3 控制台接收（DMA1_Channel3 环形缓冲） ----------------
 * 为什么要 DMA：主循环里的 hw_console_puts 是阻塞发送（一条 [RAW] 90 字节约 7.8ms @115200），
 * TIM3 的 9600Hz 采样中断又能占到约 100us，轮询或低优先级中断接收都会丢字节；DMA 环形接收
 * 不占 CPU，也不会插进采样中断的时间预算。上位机对时命令很短（26 字符以内），32 字节环足够。 */
#define CONSOLE_RX_RING_SZ 32u
static uint8_t  s_rx_ring[CONSOLE_RX_RING_SZ];
static uint16_t s_rx_tail;

static void hw_console_rx_start(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();
  USART3->CR1 &= (uint16_t)~USART_CR1_RE;      /* 配置期间先关接收 */
  (void)USART3->SR;                            /* 读 SR 再读 DR：清掉可能挂起的 RXNE/ORE */
  (void)USART3->DR;
  DMA1_Channel3->CCR = 0u;                     /* 改配置前必须先关通道 */
  DMA1_Channel3->CPAR = (uint32_t)&USART3->DR;
  DMA1_Channel3->CMAR = (uint32_t)s_rx_ring;
  DMA1_Channel3->CNDTR = CONSOLE_RX_RING_SZ;
  DMA1_Channel3->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | (2u << DMA_CCR_PL_Pos);  /* 8bit、循环、高优先级 */
  DMA1_Channel3->CCR |= DMA_CCR_EN;
  USART3->CR3 |= USART_CR3_DMAR;               /* USART3_RX 固定在 DMA1_Channel3 */
  USART3->CR1 |= USART_CR1_RE;
  s_rx_tail = 0u;
}

/* 非阻塞取 1 字节：0 = 当前没有新数据 */
uint8_t hw_console_try_getc(char *c)
{
  uint16_t head = (uint16_t)(CONSOLE_RX_RING_SZ - (uint16_t)DMA1_Channel3->CNDTR);  /* DMA 写到的位置 */
  if (!c) return 0u;
  if (head == s_rx_tail) return 0u;
  *c = (char)s_rx_ring[s_rx_tail];
  s_rx_tail++;
  if (s_rx_tail >= CONSOLE_RX_RING_SZ) s_rx_tail = 0u;
  return 1u;
}
void hw_console_init(uint32_t baud)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_USART3_CLK_ENABLE();
  g.Pin = CONSOLE_TX_PIN;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CONSOLE_GPIO, &g);
  g.Pin = CONSOLE_RX_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(CONSOLE_GPIO, &g);

  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();   /* USART3 挂在 APB1 */
  uint32_t div16 = 16u * baud;
  uint32_t mant = pclk1 / div16;
  uint32_t rem  = pclk1 % div16;
  uint32_t frac = (2u * rem + baud) / (2u * baud);
  if (frac > 15u) frac = 15u;
  USART3->BRR = (uint16_t)((mant << 4) | frac);
  USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
  hw_console_rx_start();
}

void hw_console_putc(char c)
{
  while ((USART3->SR & USART_SR_TXE) == 0u) { }
  USART3->DR = (uint8_t)c;
}


void hw_console_puts(const char *s)
{
  while (*s) hw_console_putc(*s++);
}

void hw_console_u8(uint8_t v)
{
  char b[4];
  b[0] = (char)('0' + v / 100);
  b[1] = (char)('0' + (v / 10) % 10);
  b[2] = (char)('0' + v % 10);
  b[3] = 0;
  hw_console_puts(b);
}

void hw_console_u16(uint16_t v)
{
  char b[6];
  b[0] = (char)('0' + v / 10000);
  b[1] = (char)('0' + (v / 1000) % 10);
  b[2] = (char)('0' + (v / 100) % 10);
  b[3] = (char)('0' + (v / 10) % 10);
  b[4] = (char)('0' + v % 10);
  b[5] = 0;
  hw_console_puts(b);
}


void hw_console_u32(uint32_t v)
{
  char b[11];
  uint8_t n = 0;
  if (v == 0u) { hw_console_putc('0'); return; }
  char t[10];
  uint8_t m = 0;
  while (v > 0u && m < 10u) { t[m++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (m > 0u) b[n++] = t[--m];
  b[n] = '\0';
  hw_console_puts(b);
}void hw_console_hex16(uint16_t v)
{
  static const char hx[] = "0123456789ABCDEF";
  char b[5];
  b[0] = hx[(v >> 12) & 0x0Fu];
  b[1] = hx[(v >> 8) & 0x0Fu];
  b[2] = hx[(v >> 4) & 0x0Fu];
  b[3] = hx[v & 0x0Fu];
  b[4] = 0;
  hw_console_puts(b);
}

void hw_console_hex8(uint8_t v)
{
  static const char hx[] = "0123456789ABCDEF";
  hw_console_putc(hx[(v >> 4) & 0x0Fu]);
  hw_console_putc(hx[v & 0x0Fu]);
}

/* ---------------- 定时器（寄存器级） ---------------- */
static uint32_t timer_clock_hz(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  uint32_t pre = (RCC->CFGR & RCC_CFGR_PPRE1) >> 8u;
  return (pre < 4u) ? pclk1 : (pclk1 * 2u);   /* APB1 预分频>1 时定时器时钟为 2x */
}

void hw_timers_init(void)
{
  uint32_t tclk = timer_clock_hz();
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_ADC1_CLK_ENABLE();

  /* PA1 = ADC1_IN1 模拟输入（外部已有 104 耦合 + 偏置分压） */
  g.Pin = AF_CAPTURE_PIN;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(AF_CAPTURE_GPIO, &g);

  /* ADC 时钟 = PCLK2/6 = 12MHz（最大 14MHz）；单通道 PA1 */
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV6;
  ADC1->CR1 = 0u;
  ADC1->CR2 = ADC_CR2_ADON;
  for (volatile uint32_t i = 0; i < 10000u; i++) { }   /* ADC 上电稳定 */
  ADC1->CR2 |= ADC_CR2_RSTCAL;
  while (ADC1->CR2 & ADC_CR2_RSTCAL) { }
  ADC1->CR2 |= ADC_CR2_CAL;
  while (ADC1->CR2 & ADC_CR2_CAL) { }
  ADC1->SQR1 = 0u;                                   /* 1 个转换 */
  ADC1->SQR3 = 1u;                                   /* 通道 1 = PA1 */
  ADC1->SMPR2 = (uint16_t)((ADC1->SMPR2 & ~ADC_SMPR2_SMP1) | (5u << ADC_SMPR2_SMP1_Pos)); /* 55.5 周期 */
  ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_EXTTRIG | (7u << ADC_CR2_EXTSEL_Pos); /* SWSTART */

  /* TIM3 更新中断 ~9600Hz 采样 */
  uint32_t tick = tclk / 9600u;
  TIM3->PSC = 0u;
  TIM3->ARR = (uint16_t)(tick - 1u);
  TIM3->DIER |= 0x0001u;   /* UIE */
  TIM3->CR1 |= 0x0001u;    /* CEN */
  NVIC_EnableIRQ(TIM3_IRQn);
}


/* ---------------- 独立看门狗 ---------------- */
void hw_watchdog_init(uint32_t ms)
{
  /* 调试暂停时冻结 IWDG，避免调试时被复位 */
  DBGMCU->CR |= DBGMCU_CR_DBG_IWDG_STOP;
  /* LSI ~40kHz，预分频 64 -> 625Hz */
  uint32_t reload = (ms * 625u) / 1000u;
  if (reload > 4095u) reload = 4095u;
  if (reload < 1u) reload = 1u;
  IWDG->KR = 0x5555u;              /* 解锁 */
  IWDG->PR = 0x04u;                /* /64 */
  IWDG->RLR = (uint16_t)reload;
  IWDG->KR = 0xAAAAu;              /* 先喂一次 */
  IWDG->KR = 0xCCCCu;              /* 启动 */
}

void hw_watchdog_feed(void)
{
  IWDG->KR = 0xAAAAu;
}





/* TIM3 ISR 负载统计（DWT 周期计数）。ISR 周期 = 1/9600Hz = 104us，是硬预算。
 * 用途：实机 A/B 时确认软判决的 16+9 路累加没有把 ISR 顶爆（PLAN §10.3 验收项）。 */
static uint16_t s_isr_us_last = 0, s_isr_us_max = 0;
static uint32_t s_isr_us_sum = 0, s_isr_cnt = 0;

void hw_isr_stats(uint16_t *last_us, uint16_t *max_us, uint32_t *avg_x100, uint32_t *cnt)
{
  if (last_us)  *last_us  = s_isr_us_last;
  if (max_us)   *max_us   = s_isr_us_max;
  if (avg_x100) *avg_x100 = s_isr_cnt ? (uint32_t)((s_isr_us_sum * 100u) / s_isr_cnt) : 0u;
  if (cnt)      *cnt      = s_isr_cnt;
}

void TIM3_IRQHandler(void)
{
  uint32_t t_isr0 = DWT->CYCCNT;
  if (TIM3->SR & 0x0001u) {   /* UIF */
    TIM3->SR = (uint16_t)~0x0001u;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t guard = 0;
    while (((ADC1->SR & ADC_SR_EOC) == 0u) && (guard++ < 2000u)) { }
    if (ADC1->SR & ADC_SR_EOC) {
      uint16_t v = (uint16_t)ADC1->DR;
      modem_adc_sample(v);
    } else {
      (void)ADC1->DR;   /* 超时：丢弃本次采样，避免中断死等 */
    }
  }
  {   /* ISR 耗时（us）：预算 104us */
    uint32_t dt = (uint32_t)((DWT->CYCCNT - t_isr0) / (SystemCoreClock / 1000000u));
    s_isr_us_last = (dt > 0xFFFFu) ? 0xFFFFu : (uint16_t)dt;
    if (s_isr_us_last > s_isr_us_max) s_isr_us_max = s_isr_us_last;
    s_isr_us_sum += s_isr_us_last;
    s_isr_cnt++;
  }
}
