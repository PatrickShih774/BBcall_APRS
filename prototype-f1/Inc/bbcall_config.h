#ifndef BBCALL_CONFIG_H
#define BBCALL_CONFIG_H

/*
 * BBcall_APRS 硬件配置（初版建议）
 * 引脚以 STM32F103C8T6 最小系统板 + 实际接线为准，改这里即可。
 * 对应 PLAN.md 的引脚分配表。
 */

/* ---------- BK4802（GPIO 模拟 I2C） ---------- */
#define BK4802_I2C_GPIO        GPIOA
#define BK4802_SCL_PIN         GPIO_PIN_4
#define BK4802_SDA_PIN         GPIO_PIN_5
/* 设备写地址（7-bit 0x48 左移一位） */
#define BK4802_I2C_ADDR_W      0x90

/* ---------- 音量 / 解调输出（reg19 字段，B13:B12 幅度, B03:B00 音量） ---------- */
#define BK4802_DEMOD_AMP_LEVEL 2     /* 0..3, 越大越利于 AFSK 判频 */
#define BK4802_AUDIO_VOL       15    /* 0..15 音量（关功放静音、取解调结果用） */
#define BK4802_IF_GAIN         7     /* 0..7, 3dB/级 */
#define BK4802_IF_BW_WIDE      1     /* 1=1MHz 中频滤波带宽, 0=200kHz */

/* ---------- ST7567 LCD（GPIO 模拟 SPI，4 线：CS/CLK/MOSI/A0 + RST） ---------- */
#define LCD_GPIO               GPIOB
#define LCD_CS_PIN             GPIO_PIN_6
#define LCD_CLK_PIN            GPIO_PIN_3
#define LCD_MOSI_PIN           GPIO_PIN_5
#define LCD_A0_PIN             GPIO_PIN_4      /* 0=命令, 1=数据 */
#define LCD_RST_PIN            GPIO_PIN_7      /* 低有效 */
#define LCD_BL_GPIO            GPIOA
#define LCD_BL_PIN             GPIO_PIN_8      /* 背光, 高亮 */

/* ---------- 告警 / 按键 / 状态 ---------- */
#define BUZZ_GPIO              GPIOA
#define BUZZ_PIN               GPIO_PIN_6
#define VIB_GPIO               GPIOA
#define VIB_PIN                GPIO_PIN_7
#define LED_GPIO               GPIOB
#define LED_PIN                GPIO_PIN_15
#define KEY_UP_GPIO            GPIOB
#define KEY_UP_PIN             GPIO_PIN_12
#define KEY_DOWN_GPIO          GPIOB
#define KEY_DOWN_PIN           GPIO_PIN_13
#define KEY_OK_GPIO            GPIOB
#define KEY_OK_PIN             GPIO_PIN_14

/* ---------- 串口（USART1, 调试/配置） ---------- */
#define CONSOLE_USART          huart1
#define CONSOLE_BAUD           115200

/* ---------- 信道 ---------- */
#ifndef BBCALL_DEF_FREQ_MHZ
#define BBCALL_DEF_FREQ_MHZ    144.640f
#endif

#endif /* BBCALL_CONFIG_H */
