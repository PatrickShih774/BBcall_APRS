/*
 * BK4802 驱动（STM32F103C8T6, GPIO 位敲 I2C）
 * 寄存器取值来源：MM-Radio(doublehan07) / BG7QKU / FMO(BG5ESN) 驱动。
 * 注意：reg13/reg20/21 与芯片版本有关，实板须上电校验（见 PLAN 第 6 节）。
 */
#include "bk4802.h"
#include "bbcall_config.h"

#include "stm32f1xx_hal.h"

/* ---------- 微秒延时（DWT 若可用则用 DWT，否则空循环） ---------- */
static void delay_us(uint32_t us)
{
    /* F103C8T6 @72MHz, 近似 1us 约 72 个循环（未精确，够位敲 I2C用） */
    volatile uint32_t n = us * 72UL / 4UL;
    while (n--) { __asm volatile ("nop"); }
}

/* ---------- GPIO 宏 ---------- */
#define I2C_SCL_HI()  HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SCL_PIN, GPIO_PIN_SET)
#define I2C_SCL_LO()  HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SCL_PIN, GPIO_PIN_RESET)
#define I2C_SDA_HI()  HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SDA_PIN, GPIO_PIN_SET)
#define I2C_SDA_LO()  HAL_GPIO_WritePin(BK4802_I2C_GPIO, BK4802_SDA_PIN, GPIO_PIN_RESET)

/* ---------- 寄存器表（RX 配置, reg4..reg22, 参考 MM-Radio） ---------- */
typedef struct { uint8_t addr; uint16_t data; } bk4802_reg_t;

static const bk4802_reg_t kRxDefault[] = {
    { 4,  0x0300 },  /* 打开接收相关块 */
    { 5,  0x0C04 },
    { 6,  0xF140 },
    { 7,  0xED00 },  /* IF 增益 21dB */
    { 8,  0x17E0 },
    { 9,  0xE0E0 },  /* 锁定 PLL, 射频开关 RX */
    { 10, 0x8543 },
    { 11, 0x0700 },
    { 12, 0xA066 },
    { 13, 0xFFFF },  /* 收发控制（待实板校验, 保持接收） */
    { 14, 0xFFE0 },
    { 15, 0x07A0 },
    { 16, 0x9E3C },
    { 17, 0x1F00 },
    { 18, 0xD1C7 },  /* 扬声器开关时序（用于音频连续输出） */
    { 19, 0x200F },  /* CIC 增益0dB, 解调幅度=2, 音量=15 */
    { 20, 0x01FF },  /* AFC */
    { 21, 0xE000 },
    { 22, 0x0300 },  /* 静噪阈值=0, 音频常开 */
};
#define KRX_N (sizeof(kRxDefault)/sizeof(kRxDefault[0]))

/* ---------- I2C 原语 ---------- */
static void i2c_start(void)
{
    I2C_SDA_HI();
    I2C_SCL_HI();
    delay_us(5);
    I2C_SDA_LO();
    delay_us(5);
    I2C_SCL_LO();
    delay_us(5);
}

static void i2c_stop(void)
{
    I2C_SCL_LO();
    I2C_SDA_LO();
    delay_us(5);
    I2C_SCL_HI();
    I2C_SDA_HI();
    delay_us(5);
}

static void i2c_write_byte_send(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) I2C_SDA_HI();
        else               I2C_SDA_LO();
        delay_us(2);
        I2C_SCL_HI();
        delay_us(2);
        I2C_SCL_LO();
        delay_us(2);
    }
    /* ACK 位：主释放 SDA, 期待从拉低（不严格校验） */
    I2C_SDA_HI();
    delay_us(2);
    I2C_SCL_HI();
    delay_us(2);
    I2C_SCL_LO();
}

/* 读 8 bit；最后一个字节 NACK */
static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t b = 0;
    GPIO_InitTypeDef gin = {0};
    gin.Pin   = BK4802_SDA_PIN;
    gin.Mode  = GPIO_MODE_INPUT;
    gin.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(BK4802_I2C_GPIO, &gin);

    for (int i = 7; i >= 0; i--) {
        I2C_SCL_HI();
        delay_us(2);
        b = (uint8_t)((b << 1) | (HAL_GPIO_ReadPin(BK4802_I2C_GPIO, BK4802_SDA_PIN) ? 1 : 0));
        I2C_SCL_LO();
        delay_us(2);
    }

    /* 重切为输出后发 ACK/NACK */
    GPIO_InitTypeDef gout = {0};
    gout.Pin   = BK4802_SDA_PIN;
    gout.Mode  = GPIO_MODE_OUTPUT_PP;
    gout.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BK4802_I2C_GPIO, &gout);
    if (ack) I2C_SDA_LO();
    else     I2C_SDA_HI();
    delay_us(2);
    I2C_SCL_HI();
    delay_us(2);
    I2C_SCL_LO();
    I2C_SDA_HI();
    return b;
}

void bk4802_write_reg(uint8_t reg, uint16_t data)
{
    i2c_start();
    i2c_write_byte_send(BK4802_I2C_ADDR_W);
    i2c_write_byte_send(reg);
    i2c_write_byte_send((uint8_t)(data >> 8));
    i2c_write_byte_send((uint8_t)(data & 0xFF));
    i2c_stop();
    delay_us(100);
}

uint16_t bk4802_read_reg(uint8_t reg)
{
    uint16_t hi, lo;
    i2c_start();
    i2c_write_byte_send(BK4802_I2C_ADDR_W);   /* 写地址 */
    i2c_write_byte_send(reg);                  /* 寄存器 */
    i2c_start();                               /* restart */
    i2c_write_byte_send(BK4802_I2C_ADDR_W | 1);/* 读地址 0x91 */
    hi = i2c_read_byte(1);                     /* ACK */
    lo = i2c_read_byte(0);                     /* NACK */
    i2c_stop();
    return (uint16_t)((hi << 8) | lo);
}

/* ---------- 初始化 ---------- */
void bk4802_init(void)
{
    GPIO_InitTypeDef gout = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gout.Pin   = BK4802_SCL_PIN | BK4802_SDA_PIN;
    gout.Mode  = GPIO_MODE_OUTPUT_PP;
    gout.Pull  = GPIO_NOPULL;
    gout.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BK4802_I2C_GPIO, &gout);
    I2C_SDA_HI();
    I2C_SCL_HI();
}

/* ---------- 频率 ---------- */
/* 频段 -> Ndiv / reg2 */
typedef struct { float lo, hi; uint32_t ndiv; uint16_t reg2; } band_t;
static const band_t kBands[] = {
    { 384.0f, 512.0f, 4, 0x0002 },
    { 128.0f, 170.0f, 12, 0x2004 },
    { 43.0f, 57.0f, 36, 0x8008 },
    { 35.0f, 46.0f, 44, 0xA00A },
    { 24.0f, 32.0f, 64, 0xC00F },
};
#define NBANDS (sizeof(kBands)/sizeof(kBands[0]))

void bk4802_set_rx_freq_mhz(float mhz)
{
    uint32_t ndiv = 12, value = 0;
    uint16_t reg2 = 0x2004;
    for (uint32_t i = 0; i < NBANDS; i++) {
        if (mhz >= kBands[i].lo && mhz <= kBands[i].hi) {
            ndiv = kBands[i].ndiv;
            reg2 = kBands[i].reg2;
            break;
        }
    }
    /* BK4802N: XTAL=21.25MHz；RX PLL 锁定 LO=RF-IF，IF=137kHz */
    double d = ((double)mhz - 0.137) * (double)ndiv * 16777216.0 / 21.25;
    value = (uint32_t)(d + 0.5);
    /* 写入顺序与 BG7QKU / MM-Radio DBH_SetRXFreq 一致：reg2 -> reg1(低16) -> reg0(高16) */
    bk4802_write_reg(2, reg2);
    bk4802_write_reg(1, (uint16_t)(value & 0xFFFF));
    bk4802_write_reg(0, (uint16_t)(value >> 16));
}

/* ---------- 接收 / 音频 ---------- */
void bk4802_enter_rx(void)
{
    for (uint32_t i = 0; i < KRX_N; i++)
        bk4802_write_reg(kRxDefault[i].addr, kRxDefault[i].data);
    bk4802_apply_audio_config();
}

void bk4802_set_demod_amp(uint8_t level)
{
    uint16_t r = bk4802_read_reg(19);
    r &= (uint16_t)~(0x3000u);                 /* 清 B13:B12 */
    r |= (uint16_t)((level & 0x03) << 12);     /* 设幅度 */
    bk4802_write_reg(19, r);
}

void bk4802_set_volume(uint8_t vol)
{
    uint16_t r = bk4802_read_reg(19);
    r &= 0xFFF0u;                              /* 清 B03:B00 */
    r |= (vol & 0x0F);
    bk4802_write_reg(19, r);
}

void bk4802_set_if_gain(uint8_t gain)
{
    uint16_t r = bk4802_read_reg(12);
    r &= (uint16_t)~(0x00E0u);                 /* 清 B07:B05 */
    r |= (uint16_t)((gain & 0x07) << 5);
    bk4802_write_reg(12, r);
}

void bk4802_set_if_bandwidth(uint8_t wide)
{
    uint16_t r = bk4802_read_reg(12);
    if (wide) r |= 0x0010u;
    else      r &= 0xFFEFu;
    bk4802_write_reg(12, r);
}

void bk4802_set_squelch(uint8_t thr)
{
    uint16_t r = bk4802_read_reg(22);
    r &= 0xFF00u;                              /* 清 B07:B00 */
    r |= (thr & 0xFF);
    bk4802_write_reg(22, r);
}

void bk4802_apply_audio_config(void)
{
    /* 直接整写 reg19：B15:B14=CIC 增益 0dB, B13:B12=解调幅度, B11:B04 保留, B03:B00=音量 */
    uint16_t r19 = (uint16_t)(0x0000 | ((BK4802_DEMOD_AMP_LEVEL & 0x03) << 12)
                                | (BK4802_AUDIO_VOL & 0x0F));
    bk4802_write_reg(19, r19);
    bk4802_set_if_gain(BK4802_IF_GAIN);
    bk4802_set_if_bandwidth(BK4802_IF_BW_WIDE);
}

/* ---------- S-meter 近似读取 ---------- */
uint8_t bk4802_get_smeter(void)
{
    /* 候选：SAR ADC 读到的 RSSI 寄存器，具体索引待上电用示波器/逻辑分析确认。
     * 先读一个宽范围寄存器并做简单映射（占位，需实板标定）。 */
    uint16_t raw = bk4802_read_reg(24);
    /* 假设 raw 高字节为 RSSI, 0..255 -> 1..9 档 */
    uint8_t rssi = (uint8_t)(raw >> 8);
    uint8_t s = 1;
    if (rssi > 20)  s = 2;
    if (rssi > 60)  s = 3;
    if (rssi > 100) s = 4;
    if (rssi > 140) s = 5;
    if (rssi > 175) s = 6;
    if (rssi > 205) s = 7;
    if (rssi > 230) s = 8;
    if (rssi > 248) s = 9;
    return s;
}
