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
/* 面板安装方向：0 = 正装；1 = 旋转 180° 安装（上下 + 左右一起翻）。
 * 180° 安装时 SEG 与 COM 同时反向，列起点也随之移到另一端。 */
#ifndef LCD_MOUNT_180
#define LCD_MOUNT_180  1
#endif

/* 本机模组是 132 列驱动 + 128 列面板，可见 SEG 只占其中 128 列：
 *   正装   ：可见 SEG 从芯片第 4 列开始，写数据要从列 4 起，否则画面整体左偏 4 像素；
 *   180°  ：两端互换，改成从第 0 列起。
 * 数值可按实板微调，0 = 不偏移。 */
#ifndef LCD_COL_OFFSET
#if LCD_MOUNT_180
#define LCD_COL_OFFSET  0u
#else
#define LCD_COL_OFFSET  4u
#endif
#endif
#define LCD_GPIO           GPIOB
#define LCD_CS_PIN         GPIO_PIN_6
#define LCD_CLK_PIN        GPIO_PIN_3
#define LCD_MOSI_PIN       GPIO_PIN_5
#define LCD_A0_PIN         GPIO_PIN_4
#define LCD_RST_PIN        GPIO_PIN_7
#define LCD_BL_GPIO        GPIOB
#define LCD_BL_PIN         GPIO_PIN_0

/* ---------- S-meter（BK4802 寄存器 24）采样周期 ----------
 * 锁屏/收件箱显示的 RSSI/SNR 来自这里：周期采样 + 取接收窗口峰值。
 * 置 0 表示完全不采样（界面上 RSSI/SNR 显示 --）——用于排查
 * "I2C 读取是否干扰了解码"这类问题（读失败会触发总线恢复脉冲）。 */
#ifndef BBCALL_SMETER_POLL_MS
#define BBCALL_SMETER_POLL_MS 100u
#endif

/* ---------- 锁屏页默认墙钟（无 RTC 时用） ----------
 * 开机即从这一刻走：大格显示 HH:MM，下一行显示 周W M/D。
 * 置 BBCALL_WALLCLOCK_ENABLE 0 则退回"开机时长 UP HH:MM"（design.md 的原始留白做法）。
 * 接上 RTC / 串口对时后，用 ui_set_wallclock() + ui_set_clock_ms() 覆盖即可。 */
#define BBCALL_WALLCLOCK_ENABLE 1
#define BBCALL_WALLCLOCK_WDAY   3    /* 0=周日, 1=周一 ... 6=周六 */
#define BBCALL_WALLCLOCK_MON    9
#define BBCALL_WALLCLOCK_DAY    16
#define BBCALL_WALLCLOCK_HH     20
#define BBCALL_WALLCLOCK_MM     45

#if BBCALL_WALLCLOCK_ENABLE
#define BBCALL_WALLCLOCK_BASE_MS  ((((uint32_t)BBCALL_WALLCLOCK_HH * 60u) + BBCALL_WALLCLOCK_MM) * 60000u)
#else
#define BBCALL_WALLCLOCK_BASE_MS  0u
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
#ifndef BK4802_IF_GAIN_CODE
#define BK4802_IF_GAIN_CODE  4u  /* reg7 中频增益：上电初始档，4 x 3dB = 12dB */
#endif

/* ---------- 接收中频增益（reg7 B15:B13，0..7 = 0..21dB，3dB/级） ----------
 * 这是"接收增益"的主旋钮：调大 = 灵敏度更高，但强信号更容易压缩/破音。
 *   想更灵敏：BK4802_IF_GAIN_MAX 改 7（允许 21dB）、或把 AGC_UP_RSSI 调大（更早升档）；
 *   强信号破音：BK4802_IF_GAIN_MIN 改 3/2（允许降到 9/6dB）、或把 AGC_DN_RSSI 调小；
 *   做固定增益 A/B：BBCALL_IF_AGC 置 0，只按 BK4802_IF_GAIN_CODE 工作。
 * 注意：RSSI（reg24）是在中频链路之后测的，换档会改变 RSSI 读数，跨档比较无意义。 */
#ifndef BBCALL_IF_AGC
#define BBCALL_IF_AGC        1     /* 1=自动按 RSSI 调档，0=固定 BK4802_IF_GAIN_CODE */
#endif
#ifndef BK4802_IF_GAIN_MIN
#define BK4802_IF_GAIN_MIN   4u    /* 自动可降到的最低档：4 = 12dB */
#endif
#ifndef BK4802_IF_GAIN_MAX
#define BK4802_IF_GAIN_MAX   6u    /* 自动可升到的最高档：6 = 18dB（21dB 需改 7） */
#endif
#ifndef BK4802_AGC_UP_RSSI
#define BK4802_AGC_UP_RSSI   90u   /* RSSI 低于此值 -> 提高一档 */
#endif
#ifndef BK4802_AGC_DN_RSSI
#define BK4802_AGC_DN_RSSI   115u  /* RSSI 高于此值 -> 降低一档 */
#endif
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
