# BBcall_APRS 接线与工程配置（STM32F103C8T6）

## 1. 引脚分配（与 `Inc/bbcall_config.h` 一致）

| 功能 | STM32F103C8T6 引脚 | 说明 |
|---|---|---|
| BK4802 SCL | PA4 | GPIO 输出，位敲 I2C |
| BK4802 SDA | PA5 | GPIO 输出（写）/输入（读） |
| BK4802 音频 AF | PA1 | FM 解调输出，经 RC 高通+偏置 → TIM2_CH2 输入捕获 |
| LCD CS | PB6 | GPIO 输出 |
| LCD CLK | PB3 | GPIO 输出（位敲 SPI） |
| LCD MOSI | PB5 | GPIO 输出 |
| LCD A0（DC） | PB4 | GPIO 输出 |
| LCD RST | PA8 | GPIO 输出，低有效 |
| LCD 背光 | PB7 | GPIO 输出，高亮 |
| 蜂鸣器 | PA6 | GPIO 输出（无源蜂鸣 PWM） |
| 振动马达 | PA7 | GPIO 输出（经驱动管） |
| LED | PB15 | GPIO 输出 |
| 按键 上/下/确定 | PB12/PB13/PB14 | GPIO 输入，上拉 |
| USART1 TX/RX | PA9/PA10 | 115200-8N1 |

> 建议：TIM2_CH2=PA1（AF1），TIM3 作为采样定时器；SPI 走 GPIO 位敲，避免 F1 的 SPI1 引脚复用冲突。

## 2. STM32CubeIDE 配置要点

1. 新建工程选 **STM32F103C8Tx**，时钟源 HSE=8MHz，PLL×9 → SYSCLK 72MHz。
2. 系统时钟树：AHB=1、APB1=2、APB2=1。
3. 外设：
   - USART1：Asynchronous，115200，8N1。
   - TIM2：Counter Clock 使能，CH2 设为 Input Capture direct，模式 rising，预分频器使 1MHz（PSC = 71），使能中断。
   - TIM3：Update 中断，PSC=0，ARR=7499（约 9.6kHz），使能中断。
   - GPIO：按上表设置输出/输入，注意 PA1 复用到 TIM2_CH2；其余 GPIO 均可。
4. 将 `firmware/Inc`、`firmware/Src` 加入工程 include/source 路径。
5. 生成代码后把本仓库的 `main.c`/`bk4802.c`/`lcd_st7567.c`/`modem.c`/`ax25.c`/`aprs.c` 替换进工程，并保留 CubeIDE 自动生成的 `stm32f1xx_it.c`，在其中将 `TIM2_IRQHandler`/`TIM3_IRQHandler` 调用 `HAL_TIM_IRQHandler(&htim2/htim3)`（本仓库 main.c 已含相关函数；若冲突删除其一）。

## 3. 音频引出（AFSK 判频）

- 从 BK4802 FM 解调输出（功放前级，reg19 幅度可控）引出。
- 经 0.1µF 高通 + 偏置电阻分压至 VDD/2 → PA1。
- 若信号幅度不足，加一级比较器/施密特整形再送 PA1（用于定时器捕获周期），或直接送 ADC 采样走 Goertzel。
- 上电先断 BK4802 静噪/门控，保证解调音频恒定输出。

## 4. 天线

- 2m（144.640MHz）：用 50Ω 同轴接 SMA，焊上 2m 手台天线或 1/4 波长拉杆。
- 焊点尽量短；参考 MM-Radio 的滤波器短接/飞线流程。

## 5. 烧录

- ST-Link：`make flash` 或 STM32CubeProgrammer 烧录 `.bin`。
- J-Link：`JLinkExe -device STM32F103C8 -if SWD -speed 4000 -autoconnect 1 -CommandFile flash.jlink`。
