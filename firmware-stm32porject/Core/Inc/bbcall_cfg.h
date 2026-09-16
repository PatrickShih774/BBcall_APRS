#ifndef BBCALL_CFG_H
#define BBCALL_CFG_H
/*
 * BBcall_APRS 硬件配置（初版建议，按实板改这里）
 * 芯片：STM32F103C8Tx（Blue Pill）
 */
/* ---------- BK4802 引脚（与 MM-Radio 保持一致） ---------- */
#define BK4802_I2C_GPIO    GPIOA
#define BK4802_SCL_PIN     GPIO_PIN_9    /* MM-Radio: CLK */
#define BK4802_SDA_PIN     GPIO_PIN_10   /* MM-Radio: DATA */
#define BK4802_CE_GPIO     GPIOA
#define BK4802_CE_PIN      GPIO_PIN_0    /* MM-Radio: CE 使能，上电拉高 */
#define BK4802_DIO1_GPIO   GPIOA
#define BK4802_DIO1_PIN    GPIO_PIN_8    /* MM-Radio: DIO1，输出低 */
#define BK4802_I2C_ADDR_W  0x90u         /* 7-bit 0x48 左移一位 */
/* ---------- 音频输入（PA1 = ADC1_IN1 + TIM2_CH2；当前固件走 ADC 解调） ---------- */
#define AF_CAPTURE_GPIO    GPIOA
#define AF_CAPTURE_PIN     GPIO_PIN_1
/* ---------- ST7567 LCD（GPIO 模拟 SPI，4 线；背光让出 PA8 给 DIO1） ---------- */
#define LCD_GPIO           GPIOB
#define LCD_CS_PIN         GPIO_PIN_6
#define LCD_CLK_PIN        GPIO_PIN_3
#define LCD_MOSI_PIN       GPIO_PIN_5
#define LCD_A0_PIN         GPIO_PIN_4
#define LCD_RST_PIN        GPIO_PIN_7
#define LCD_BL_GPIO        GPIOB
#define LCD_BL_PIN         GPIO_PIN_0

/* 中文字库：1 = 启用（需要 Core/Inc/cn_font_data.h，由 tools/gen_cn_font.py 生成）。
 * 16x16 点阵，每字 36 字节（位图 32 + 索引 4）；STM32F103C8T6 只有 64KB Flash，
 * 所以只放子集（当前 107 字约 3.9KB），全字库需外置 SPI Flash（见 PLAN 中文显示一节）。
 * 注：UI v2.0 三态界面改用 Fusion Pixel 字模（fusion_font.h），不再走本链路。 */
#ifndef CN_FONT_ENABLED
#define CN_FONT_ENABLED   0
#endif

/* ---------- UI v2.0 三态界面（design.md v2.0；唯一权威规范） ----------
 * BBCALL_LCD_ENABLED = 1：启用 ST7567 LCD 三态界面（待机/有未读/收件箱）。
 * LCD 未焊接时可置 0，退回纯串口调试模式。源文件：Core/Src/ui_harness.c
 * （模拟器与真机共用同一文件，LCD_SIM 区分底层）。 */
#ifndef BBCALL_LCD_ENABLED
#define BBCALL_LCD_ENABLED 1
#endif
/* 本机呼号：显示在待机页右上格；改成你的呼号 */
#ifndef BBCALL_MYCALL
#define BBCALL_MYCALL "BG5BLH"
#endif

/* ---------- 告警 / 按键 / 状态 ---------- */
#define BUZZ_GPIO          GPIOA
#define BUZZ_PIN           GPIO_PIN_6
#define VIB_GPIO           GPIOA
#define VIB_PIN            GPIO_PIN_7
#define LED_GPIO           GPIOB
#define LED_PIN            GPIO_PIN_15
#define KEY_UP_GPIO        GPIOB
#define KEY_UP_PIN         GPIO_PIN_12
#define KEY_DOWN_GPIO      GPIOB
#define KEY_DOWN_PIN       GPIO_PIN_13
#define KEY_OK_GPIO        GPIOB
#define KEY_OK_PIN         GPIO_PIN_14
/* ---------- 对讲机侧按键（MM-Radio: PA2=PWR, PA4=PTT, 上升沿/无内拉） ---------- */
#define KEY_PWR_GPIO       GPIOA
#define KEY_PWR_PIN        GPIO_PIN_2
#define KEY_PTT_GPIO       GPIOA
#define KEY_PTT_PIN        GPIO_PIN_4
/* ---------- 调试串口 USART3（PB10=TX, PB11=RX, 寄存器级） ---------- */
#define BBCALL_CONSOLE_ENABLED 1u
#define CONSOLE_GPIO      GPIOB
#define CONSOLE_TX_PIN    GPIO_PIN_10
#define CONSOLE_RX_PIN    GPIO_PIN_11
#define CONSOLE_BAUD      115200u
/* ---------- BK4802 音频/接收参数 ---------- */
/* 强信号下先把增益从最大档降下来，避免喇叭削波：
 * reg19 B13:B12 = FM 解调输出幅度（0..3）
 * reg19 B03:B00 = 接收音量（0..15）
 * reg7  B15:B13 = 中频增益，3dB/级（0=0dB ... 7=21dB） */
#define BK4802_CIC_GAIN      2u  /* reg19 B15:B14 CIC 增益 2=3.5dB */
#define BK4802_DEMOD_AMP     1u  /* reg19 B13:B12 解调幅度（第 1 档） */
#define BK4802_AUDIO_VOL    15u  /* reg19 音量 */
#define BK4802_IF_GAIN_CODE  4u  /* reg7 中频增益 4×3dB = 12dB（折中） */
/* reg22 静噪：B11:B10=噪声阈值倍率(0:x2,1:x4,...)，B07:B00=RSSI 关闭阈值。
 * reg23 B07:B00=开喇叭的带外噪声阈值（越小越难打开）。
 * 实测：无信号 RSSI≈59/EXN≈321；强信号 RSSI≈127/EXN≈5。 */
#define BK4802_SQ_MULT        3u   /* 噪声倍率 x16 */
#define BK4802_SQ_RSSI_THR    0u   /* RSSI 阈值为 0：不靠 RSSI 关门 */
#define BK4802_SQ_NOISE_THR  0xFFu /* 噪声上限 255×16：始终满足开喇叭条件 */
/* 软件静噪总开关：0=关闭（音频常开，PA1 才能持续解码）；1=启用 */
#define BBCALL_SW_SQUELCH     0u
/* 串口 RAW 十六进制输出：0=不打印（减少串口占用，利于连续接收+解码） */
#define BBCALL_RAW_LOG        1u
/* ---------- 默认接收频率 ---------- */
#ifndef BBCALL_DEF_FREQ_MHZ
#define BBCALL_DEF_FREQ_MHZ 144.64
#endif
#endif /* BBCALL_CFG_H */
