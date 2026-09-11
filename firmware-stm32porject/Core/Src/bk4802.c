/*
 * BK4802 驱动（STM32F103C8Tx, GPIO 模拟 I2C）
 * 引脚与 MM-Radio 一致：SCL=PA9(CLK), SDA=PA10(DATA), CE=PA0, DIO1=PA8。
 * 写操作用推挽（同 MM-Radio）；读寄存器（S-meter 等）时把 SDA 临时切为输入。
 * 频率字按 FMO nDivCacl 约定生成（21.25MHz XTAL、RX LO=RF-IF，IF=137kHz）。
 */
#include "main.h"
#include "bbcall_cfg.h"
#include "bbcall_hw.h"
#include "bk4802.h"

static void scl_hi(void){ HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SCL_PIN, GPIO_PIN_SET); }
static void scl_lo(void){ HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SCL_PIN, GPIO_PIN_RESET); }
static void sda_hi(void){ HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SDA_PIN, GPIO_PIN_SET); }
static void sda_lo(void){ HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SDA_PIN, GPIO_PIN_RESET); }

/* SDA 在推挽输出 <-> 输入（带上拉）间切换，避免与从机 ACK 争用 */
static void sda_set_output(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = BK4802_SDA_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BK4802_I2C_GPIO, &g);
  sda_hi();
}

static void sda_set_input(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = BK4802_SDA_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BK4802_I2C_GPIO, &g);
}

/* I2C 错误计数与总线恢复 */
static uint16_t s_i2c_err = 0;

uint16_t bk4802_i2c_error_count(void)
{
  return s_i2c_err;
}

static void i2c_recover(void)
{
  /* 释放 SDA，发 9 个 SCL 脉冲让从机退出，再补一个 STOP */
  sda_set_input();
  for (uint8_t i = 0; i < 9u; i++) {
    scl_lo(); hw_delay_us(2);
    scl_hi(); hw_delay_us(2);
  }
  sda_set_output();
  sda_lo(); hw_delay_us(2);
  scl_hi(); hw_delay_us(2);
  sda_hi(); hw_delay_us(2);
}
static void i2c_start(void)
{
  sda_set_output();
  sda_hi(); scl_hi(); hw_delay_us(2);
  sda_lo(); hw_delay_us(2);
  scl_lo();
}

static void i2c_stop(void)
{
  sda_set_output();
  scl_lo(); sda_lo(); hw_delay_us(2);
  scl_hi(); sda_hi(); hw_delay_us(2);
}

static uint8_t i2c_byte(uint8_t b)
{
  sda_set_output();
  for (int i = 7; i >= 0; i--) {
    if (b & (1u << i)) sda_hi(); else sda_lo();
    hw_delay_us(1);
    scl_hi(); hw_delay_us(2); scl_lo(); hw_delay_us(1);
  }
  /* ACK 位：SDA 切输入，SCL 拉高后采样；返回 0=ACK，1=NACK */
  sda_set_input();
  hw_delay_us(1);
  scl_hi(); hw_delay_us(3);
  uint8_t nack = HAL_GPIO_ReadPin(BK4802_I2C_GPIO, BK4802_SDA_PIN) ? 1u : 0u;
  scl_lo(); hw_delay_us(1);
  sda_set_output();
  if (nack) s_i2c_err++;
  return nack;
}

static uint8_t i2c_read(uint8_t ack)
{
  uint8_t v = 0;
  sda_set_input();
  for (int i = 7; i >= 0; i--) {
    scl_hi();
    hw_delay_us(3);            /* 等 SDA 稳定再采样，抗射频近场干扰 */
    v = (uint8_t)((v << 1) | (HAL_GPIO_ReadPin(BK4802_I2C_GPIO, BK4802_SDA_PIN) ? 1u : 0u));
    scl_lo(); hw_delay_us(3);
  }
  sda_set_output();
  if (ack) sda_lo(); else sda_hi();
  hw_delay_us(1); scl_hi(); hw_delay_us(1); scl_lo();
  sda_hi();
  return v;
}

typedef struct { uint8_t addr; uint16_t data; } reg_t;

static const reg_t rxcfg[] = {
  {4, 0x0300}, {5, 0x0C04}, {6, 0xF140}, {7, 0xED00},
  {8, 0x17E0}, {9, 0xE0E4}, {10, 0x8543}, {11, 0x0700},
  {12, 0xA066}, {13, 0xFFFF}, {14, 0xFFE0}, {15, 0x07A0},
  /* reg18 = 0xD100：开/关喇叭延时都取最短 100ms（原来关断要 800ms，
   * 信号结束后会残留一段底噪） */
  {16, 0x9E3C}, {17, 0x1F00}, {18, 0xD100}, {19, 0x200F},
  {20, 0x01FF}, {21, 0xE000},
  /* reg22 = SQ_N(×4) | RSSI 关闭阈值 80（先放开静噪再在下方锁回 RX） */
  {22, (uint16_t)(((BK4802_SQ_MULT & 0x03u) << 10) | (BK4802_SQ_RSSI_THR & 0xFFu))},
};
#define RXCFG_N (sizeof(rxcfg)/sizeof(rxcfg[0]))

void bk4802_init(void)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* CE = PA0 输出高（使能）；DIO1 = PA8 输出低（MM-Radio 同） */
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = BK4802_CE_PIN | BK4802_DIO1_PIN;
  HAL_GPIO_Init(GPIOA, &g);
  HAL_GPIO_WritePin(GPIOA, BK4802_CE_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, BK4802_DIO1_PIN, GPIO_PIN_RESET);

  /* BK4802N 上电后需等内部 LDO/DSP 稳定再写寄存器（MM-Radio 等 1s，FMO 等 100ms） */
  HAL_Delay(100);

  /* SCL = PA9 推挽；SDA = PA10 推挽（读时切输入） */
  g.Pin = BK4802_SCL_PIN;
  HAL_GPIO_Init(BK4802_I2C_GPIO, &g);
  sda_set_output();
  scl_hi();
}

void bk4802_write_reg(uint8_t reg, uint16_t data)
{
  for (uint8_t attempt = 0; attempt < 3u; attempt++) {
    i2c_start();
    uint8_t bad = i2c_byte(BK4802_I2C_ADDR_W);
    bad |= i2c_byte(reg);
    bad |= i2c_byte((uint8_t)(data >> 8));
    bad |= i2c_byte((uint8_t)(data & 0xFF));
    i2c_stop();
    if (!bad) { hw_delay_us(50); return; }
    s_i2c_err++;
    i2c_recover();
  }
  hw_delay_us(50);
}

uint16_t bk4802_read_reg(uint8_t reg)
{
  for (uint8_t attempt = 0; attempt < 3u; attempt++) {
    i2c_start();
    uint8_t bad = i2c_byte(BK4802_I2C_ADDR_W);
    bad |= i2c_byte(reg);
    i2c_start();
    bad |= i2c_byte((uint8_t)(BK4802_I2C_ADDR_W | 1u));
    if (!bad) {
      uint16_t hi = i2c_read(1);
      uint16_t lo = i2c_read(0);
      i2c_stop();
      return (uint16_t)((hi << 8) | lo);
    }
    i2c_stop();
    s_i2c_err++;
    i2c_recover();
  }
  return 0xFFFFu;
}

void bk4802_enter_rx(void)
{
  /* BK4802P（中文手册 reg23）：
   *   B15=0 关省电；B10=1 由 reg23<9> 控制收发（不依赖 TRX 引脚）；
   *   B09=0 数字部分 RX；B08=0 PIN24 作 RSSI；B07..00=开喇叭噪声阈值。
   * MM-Radio 的 0x60FF 会把 B10 清零、交给 TRX 引脚，TRX 悬空时芯片
   * 可能停在 TX，导致 RSSI 恒 0（本板实测现象）。 */
  uint16_t reg23 = (uint16_t)(0x6400u | (BK4802_SQ_NOISE_THR & 0xFFu));
  bk4802_write_reg(23, reg23);
  for (uint32_t i = 0; i < RXCFG_N; i++)
    bk4802_write_reg(rxcfg[i].addr, rxcfg[i].data);
  /* RX 通路寄存器写完后，再把收发状态锁到“寄存器控制的 RX 态”，
   * 避免 TRX 引脚在配置过程中把 RF 开关带到发射通路。 */
  bk4802_write_reg(23, reg23);
  bk4802_write_reg(32, 0x31FFu);
  bk4802_apply_audio_config();
}

void bk4802_apply_audio_config(void)
{
  /* reg19：B15:B14=CIC 增益，B13:B12=解调幅度，B03:B00=音量 */
  uint16_t r19 = (uint16_t)(((BK4802_CIC_GAIN & 0x03u) << 14)
                          | ((BK4802_DEMOD_AMP & 0x03u) << 12)
                          | (BK4802_AUDIO_VOL & 0x0Fu));
  bk4802_write_reg(19, r19);
  /* reg7 B15:B13：BK4802P 的中频增益（旧 BK4802 才在 reg12，不能沿用） */
  uint16_t r7 = 0xED00u & (uint16_t)~0xE000u;
  r7 |= (uint16_t)((BK4802_IF_GAIN_CODE & 0x07u) << 13);
  bk4802_write_reg(7, r7);
}

void bk4802_set_if_gain_code(uint8_t code)
{
  /* reg7 B15:B13：BK4802P 接收中频增益，3dB/级（0=0dB ... 7=21dB） */
  uint16_t r7 = 0xED00u & (uint16_t)~0xE000u;
  r7 |= (uint16_t)((code & 0x07u) << 13);
  bk4802_write_reg(7, r7);
}
void bk4802_set_squelch(uint8_t thr)
{
  uint16_t r = bk4802_read_reg(22) & 0xFF00u;
  bk4802_write_reg(22, (uint16_t)(r | (thr & 0xFFu)));
}

void bk4802_set_rx_audio_mute(uint8_t mute)
{
  uint16_t r19 = (uint16_t)(((BK4802_CIC_GAIN & 0x03u) << 14)
                          | ((BK4802_DEMOD_AMP & 0x03u) << 12));
  uint16_t reg23 = (uint16_t)(0x6400u | (BK4802_SQ_NOISE_THR & 0xFFu));
  if (mute) {
    /* reg4 B11=1：关接收音频（音量0压不住 D 类功放底噪，必须断通路） */
    bk4802_write_reg(4, 0x0800u);
    bk4802_write_reg(23, reg23);
    bk4802_write_reg(19, r19);
  } else {
    /* 恢复 RX 音频：连写 reg4、重锁 reg23，防止强信号下 I2C 写丢 */
    bk4802_write_reg(4, 0x0300u);
    bk4802_write_reg(4, 0x0300u);
    bk4802_write_reg(23, reg23);
    bk4802_write_reg(4, 0x0300u);
    bk4802_write_reg(19, (uint16_t)(r19 | (BK4802_AUDIO_VOL & 0x0Fu)));
  }
}

static const struct { float lo, hi; uint32_t ndiv; uint16_t reg2; } bands[] = {
  {384.0f, 512.0f, 4, 0x0002},
  {128.0f, 170.0f, 12, 0x2004},
  {43.0f, 57.0f, 36, 0x8008},
  {35.0f, 46.0f, 44, 0xA00A},
  {24.0f, 32.0f, 64, 0xC00F},
};

void bk4802_set_rx_freq_mhz(double mhz)
{
  const double xtal_mhz = 21.25;   /* BK4802N 数据手册: 21.25MHz */
  const double if_mhz   = 0.137;   /* FMO BK4802.c: 低中频 137kHz */
  uint32_t ndiv = 12;
  uint16_t reg2 = 0x2004;
  for (uint32_t i = 0; i < sizeof(bands)/sizeof(bands[0]); i++) {
    if (mhz >= bands[i].lo && mhz <= bands[i].hi) {
      ndiv = bands[i].ndiv;
      reg2 = bands[i].reg2;
      break;
    }
  }
  /* 接收时 PLL 锁定在 LO = RF - IF（FMO nDivCacl/RX 路径同款算法） */
  double lo = (double)mhz - if_mhz;
  double d = lo * (double)ndiv * 16777216.0 / xtal_mhz;
  uint32_t value = (uint32_t)(d + 0.5);
  /* 写入顺序与 BG7QKU / MM-Radio DBH_SetRXFreq 一致：reg2 -> reg1(低16) -> reg0(高16) */
  bk4802_write_reg(2, reg2);
  bk4802_write_reg(1, (uint16_t)(value & 0xFFFFu));
  bk4802_write_reg(0, (uint16_t)(value >> 16));
}

uint8_t bk4802_get_smeter(void)
{
  /* reg24 BIT7..0 = RSSI（FMO 确认），BIT13..8 = SNR */
  uint16_t raw = bk4802_read_reg(24);
  uint8_t rssi = (uint8_t)(raw & 0x00FFu);
  static const uint8_t th[10] = {64, 70, 76, 82, 89, 97, 104, 112, 118, 125};
  uint8_t s = 0;
  for (uint8_t i = 0; i < 10u; i++) if (rssi > th[i]) s = (uint8_t)(i + 1u);
  if (s > 9u) s = 9u;
  if (s == 0u) s = 1u;   /* 显示最低档 1 */
  return s;
}
