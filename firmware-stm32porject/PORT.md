# BBcall_APRS 移植说明（STM32F103C8Tx CubeIDE 工程）

本工程是你手动创建的 CubeIDE 工程（`.ioc` 未配外设）。BBcall 代码已按「不依赖 CubeMX 生成、可在 USER CODE 区存活」的方式移植：

## 新增文件

| 文件 | 作用 |
|---|---|
| `Core/Inc/bbcall_cfg.h` | 引脚/参数配置（改接线只需改这里） |
| `Core/Inc/bbcall_hw.h` + `Core/Src/bbcall_hw.c` | 时钟(72MHz 尝试)/DWT 延时/GPIO/寄存器级 USART1/寄存器级 TIM2+TIM3 |
| `Core/Inc/bk4802.h` + `Core/Src/bk4802.c` | BK4802 位敲 I2C、RX 配置、频率字、S-meter 占位 |
| `Core/Inc/lcd_st7567.h` + `Core/Src/lcd_st7567.c` + `Core/Inc/font8x16.h` | ST7567 位敲 SPI + 8x16 ASCII 字体 |
| `Core/Inc/ax25.h` + `Core/Src/ax25.c` | CRC-16/X.25、HDLC、AX.25 解析（主机已验证） |
| `Core/Inc/aprs.h` + `Core/Src/aprs.c` | APRS 消息解析（主机已验证） |
| `Core/Inc/modem.h` + `Core/Src/modem.c` | AFSK 判频 + NRZI + HDLC 流式（骨架） |
| `Core/Inc/bbcall_app.h` + `Core/Src/bbcall_app.c` | 应用初始化/主循环 |

## main.c 改动（均在 USER CODE 区，重新生成不会丢）

- Includes：`bbcall_hw.h`、`bbcall_app.h`
- SysInit：`hw_delay_init(); hw_clock_try_72mhz();`
- USER CODE 2：`bbcall_app_init();`
- while(1)：`bbcall_app_loop();`

## 中断

`TIM2_IRQHandler` / `TIM3_IRQHandler` 定义在 `bbcall_hw.c`（寄存器级）。`stm32f1xx_it.c` 未改动。

## 引脚（默认建议）

| 功能 | 引脚 |
|---|---|
| BK4802 I2C SCL/SDA（与 MM-Radio 一致） | PA9 / PA10 |
| BK4802 CE（上电拉高使能） | PA0 |
| BK4802 DIO1（输出低） | PA8 |
| 音频捕获 TIM2_CH2 | PA1 |
| LCD CS/CLK/MOSI/A0/RST | PB6 / PB3 / PB5 / PB4 / PB7 |
| LCD 背光 | PB0 |
| 蜂鸣/振动 | PA6 / PA7 |
| LED | PB15 |
| 按键 上/下/确定 | PB12 / PB13 / PB14 |
| 对讲机侧 PWR / PTT（MM-Radio 同） | PA2 / PA4 |
| 调试串口 USART3 TX/RX | PB10 / PB11（115200） |

## 时钟策略

- 启动先用 CubeIDE 默认 HSI（保证能启动）。
- `hw_clock_try_72mhz()` 尝试 HSE 8MHz × PLL9 = 72MHz；无 8MHz 晶振则保持 HSI（波特率/定时器会按实际时钟自动计算）。

## 编译

1. STM32CubeIDE 中 **Refresh (F5)** 工程，让新加的 `.c/.h` 进入构建。
2. 直接 Build。若报错，把错误信息贴回给我。

## 上电预期（Phase 1）

- LED 点亮；LCD 显示 "BBCALL APRS RX / 144.640 MHz"。
- 串口(USART3)每 500ms 输出 `S=<1..9>`（S-meter 为占位映射，需实板标定）。
- 若有 APRS 解调帧：串口打印 `[FRAME] src=... msg=...`，LCD 显示 FROM/呼号/正文。

## 实板待验证项

- BK4802 频率字（当前按 FMO + 数据手册修正：21.25MHz XTAL、RX IF=137kHz；144.640 → reg2=0x2004/reg0=0x519A/reg1=0x08A0）与寄存器表 reg13/20/21。
- S-meter 读取寄存器索引（当前 reg24 占位）。
- ST7567 初始化序列与对比度（不同模组可能微调）。
- AF 音频引出：PA1 需 RC 高通 + 偏置到 VDD/2，信号够强才判频。
