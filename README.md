# BBcall_APRS

> **应用参考 / Application Reference**
>
> 本项目的参考项目与工程底座是 **[MM-Radio](https://github.com/doublehan07/MM-Radio)**
> （BSD 2-Clause License，Copyright (c) 2024 Han Zhang）。
> 工程结构、BK4802 驱动思路和部分寄存器初始化参考/移植自 MM-Radio；
> 依据 BSD-2-Clause 保留原作者版权与许可声明，详见「许可与合规」。
> 本项目自身以 **GPL-3.0** 发布（见根目录 `LICENSE`）。
> 许可证兼容性：**BSD-2-Clause → GPL-3.0 兼容**，不冲突。

### 快速导航

- [硬件/引脚](#2-引脚分配)
- [调试全过程（含所有踩坑记录）](#4-调试过程记录)
- [串口输出说明](#5-串口诊断字段说明)
- [硬件改进方案](#9-硬件改进方案提升解码率)
- [BB 机功能规划](PLAN.md)
- [PC LCD 模拟器](#13-pc-端-lcd-模拟器sdl2)
- [UI 设计规范 design.md](design.md)
- [许可与合规](#11-许可与合规)

用 **BK4802P（玩具对讲 FM 收发芯片）+ STM32F103C8T6 + ST7567 12864 LCD**
复刻一台 APRS 寻呼机（BB 机）。仅接收（RX-only），默认频率 **144.640MHz**，
目标是把空中收到的 APRS 数据包解出来并显示在 LCD 上。

当前状态：**RF → 音频 → ADC → 判频 → NRZI → HDLC → AX.25 → APRS 全链路已打通**；v0.3 起支持 Mic-E/普通位置解析、1/2-bit CRC 纠错、重复包辅助恢复、解码时间戳、看门狗与 I2C 健壮性，
已用实机收到并解析真实 APRS 数据包（见下文「成功解码记录」）；经解码算法优化（幅度门限 20000→500、16 相位、跳变对齐位时钟）后成功率大幅提升。LCD 尚未焊接，
当前通过 USART3（PB10/PB11，115200）输出调试信息。硬件前端目前是"天线直接接 BK4802 ANT 脚、无滤波/匹配"，这是当前弱信号解码率的主要瓶颈，改进方案见第 9 节。


---

## 1. 关键决策（原「开放问题」已确定）

| 项 | 结论 |
|---|---|
| MCU | **STM32F103C8T6**，工程目录 `firmware-stm32porject/`（用户手动建的 CubeIDE 工程） |
| 射频芯片 | **BK4802P**，21.25MHz 晶振，低中频 IF = 137kHz |
| 接收频率 | **144.640MHz**（2m），默认频点 |
| 收发 | **仅接收**，不做发射 |
| 显示 | ST7567 12864（未焊接，代码里 `BBCALL_LCD_ENABLED=0`） |
| 调试口 | USART3：PB10=TX、PB11=RX、115200 8N1 |
| 工程底座 | 主框架参照 [MM-Radio](https://github.com/doublehan07/MM-Radio)，代码移植进 F103 CubeIDE 工程 |

---

## 2. 引脚分配

定义文件：`firmware-stm32porject/Core/Inc/bbcall_cfg.h`

| 功能 | 引脚 | 说明 |
|---|---|---|
| BK4802 SCL | PA9 | GPIO 位敲 I2C（与 MM-Radio 一致） |
| BK4802 SDA | PA10 | 写：推挽；读：切输入（与 MM-Radio 一致） |
| BK4802 CE | PA0 | 输出高使能（与 MM-Radio 一致） |
| BK4802 DIO1 | PA8 | 输出低（与 MM-Radio 一致） |
| 对讲机侧 PWR / PTT | PA2 / PA4 | 与 MM-Radio 一致 |
| 音频输入 | **PA1 = ADC1_IN1** | 现走 ADC 解调；同时是 TIM2_CH2 |
| ST7567 CS/CLK/MOSI/A0/RST | PB6 / PB3 / PB5 / PB4 / PB7 | 位敲 SPI |
| LCD 背光 | PB0 | 高电平点亮 |
| 蜂鸣器 / 振动 | PA6 / PA7 | 输出 |
| LED | PB15 | 500ms 翻转，用于判断程序是否活着 |
| 按键 上/下/确定 | PB12 / PB13 / PB14 | 上拉输入 |
| 调试串口 USART3 | PB10=TX / PB11=RX | 115200 8N1 |

---

## 3. 软件结构

| 文件 | 作用 |
|---|---|
| `Core/Src/bbcall_hw.c` | 时钟(72MHz)/延时/GPIO/寄存器级 USART3/ADC1/TIM3 |
| `Core/Src/bk4802.c` | BK4802 位敲 I2C、RX 配置、频率字、增益、静噪 |
| `Core/Src/modem.c` | ADC 采样 → 1200/2200Hz 定点相关判频 → NRZI → 16 相位并行 HDLC + 跳变对齐位时钟 |
| `Core/Src/ax25.c` | CRC-16/X.25、HDLC 去填充、AX.25 地址/控制/PID/信息解析 |
| `Core/Src/aprs.c` | APRS 消息解析（信息域以 `:` 开头） |
| `Core/Src/lcd_st7567.c` | ST7567 驱动 + 8x16 字体（未启用） |
| `Core/Src/bbcall_app.c` | 初始化、主循环、串口诊断/解码输出 |

信号链：

```
BK4802P FM 接收 → EAROP 音频（D 类功放输出）
  → 104 隔直 + RC 低通 + 10k/10k 偏置
  → PA1(ADC1_IN1)
  → TIM3 9600Hz 采样（1200 baud × 8）
  → 最近 8 点对 1200/2200Hz 做定点相关
  → 16 相位并行判决（含半采样插值）+ 跳变对齐位时钟
  → NRZI（不变=1、跳变=0）
  → HDLC（0x7E 标志、去位填充、CRC-16/X.25）
  → AX.25 拆呼号 → APRS 解析 → USART3 / LCD
```

---

## 4. 调试过程记录

以下按时间顺序记录整个 bring-up 过程中遇到的现象、根因和修复，便于以后复现。

### 4.1 频率字与显示

**现象**：启动打印 `[FREQ] want r1=08A0`，读回却是 `089A`；显示频率为 144.639MHz。

**根因**：

- BK4802P 用 21.25MHz 晶振、低中频 IF=137kHz，RX 时 PLL 锁定在 `LO = RF - IF`：

  ```
  value = (RF - 0.137) × Ndiv × 2^24 / 21.25
  ```

  144.640MHz、Ndiv=12 → `reg2=0x2004 / reg0=0x519A / reg1=0x08A0`。
- 代码用 `float` 存 144.640f，实际是 144.639999…，算出的频率字低了 6 LSB（约 0.6Hz），
  格式化时又截断成 639。

**修复**：频率计算和打印改用 `double`，`BBCALL_DEF_FREQ_MHZ` 定义为 `144.64`。
修改后启动打印：

```
[BBcall] RX 144.640 MHz
[FREQ] want r2=2004 r0=519A r1=08A0
[FREQ] read r2=2004 r0=519A r1=08A0
```

### 4.2 RSSI 恒 0（收不到信号）

**现象**：`R19=08207 RSSI=00000 SNR=00000 ID=18434`，无论有无信号都不变化；
近距离强信号时读数被打成全 1（`RSSI=255 / SNR=63 / ID=65535`）。
换 437.625MHz 也一样，说明与频段无关。

**根因**：MM-Radio 初始化写 `reg23 = 0x60FF`，把 `reg23<10>` 清 0。
按 BK4802P 手册，此时收发状态改由 **TRX 引脚**控制（高=TX、低=RX）。
MM-Radio 原板 TRX 被拉低；本板 TRX 悬空，芯片停在 TX/不确定态，
RF 开关切到发射通路，RX 前端等于关闭，所以 RSSI 恒 0。

**修复**（`bk4802.c` 的 `bk4802_enter_rx()`）：

```c
/* reg23 = 0x64E0
 *   B15=0 关自动省电
 *   B10=1 收发状态由 reg23<9> 控制，不依赖 TRX 引脚
 *   B09=0 数字部分强制 RX
 *   B08=0 PIN24 保持 RSSI 输出 */
bk4802_write_reg(23, 0x64E0u);
/* rxcfg 里 reg9 = 0xE0E4：RF 开关接接收通路 */
/* 写完 reg4..22 后再次写 reg23，把状态锁死在 RX */
bk4802_write_reg(23, 0x64E0u);
```

**结果**：RSSI/SNR 随手台信号变化，`ID` 保持 0x4802，144.640MHz 实机可收。

> 结论：`reg23=0x60FF` 只在 TRX 引脚被可靠拉低的板上成立；TRX 悬空的板子
> 必须用寄存器控制（B10=1、B09=0）。

### 4.3 强信号下 I2C 读失效

**现象**：手台靠近时读到 `R19=65535 / ID=65535`。这是长导线拾取强 RF 后再做位敲 I2C，
从机 ACK/数据位被干扰，不是芯片坏。

**处理**：`bk4802_read_reg()` 的 I2C 时序里加了稳定等待；关键写入（reg23 锁 RX、
软件静噪恢复）连写两次防止被冲掉。日常使用保持 I2C 线尽量短。

### 4.4 音频引出：AUDC 不是音频输出

**问题**：想找一个"没经过功放的 FM 解调音频"引脚，怀疑是 AUDC。

**结论**：不是。BK4802P 手册里：

- `AUDC`：麦克风放大器的共模节点，只外接 1µF 到地，不是音频信号输出；
- `ASKOUT(PIN24)`：ASK 解调的数字判决输出，不能解 AFSK；
- 接收音频链全在片内：FM 解调 → 去加重 → 3.1kHz LPF → HPF → 音量 → **D 类音频功放**
  → `EAROP/EARON` 差分输出 → 喇叭。

所以可取的只有 **EAROP/EARON（或喇叭焊盘）**；
并且它是 **D 类 PWM 输出（约 50–100kHz 载波）**，不能直接给 MCU，必须滤波。

### 4.5 音频取样电路

最终电路：

```
EAROP ── 104(100nF) ── R 1kΩ ──┬── 103(10nF) ── GND
                               ├── 10kΩ ── 3.3V
                               ├── 10kΩ ── GND
                               └── PA1
```

调试中发现的问题：

- **只加 103 不加串联电阻**：104 在 100kHz 时阻抗只有约 16Ω，低通截止仍在 ~100kHz，
  PWM 载波原样进来，表现为 `C`/`MK` 每秒几万次；必须串 1k（和 103 组成 ~16kHz 低通）。
- **喇叭是 D 类功放的负载**：喇叭接着时输出被拉低；把喇叭去掉后 PA1 摆幅可到 2Vpp。
- **偏置**：10k/10k 把静态点定在 ~1.5V；F103 的门限约 1.16V/1.83V，
  所以音频摆幅需要 >0.67Vpp 才能稳定翻转（后改走 ADC 后不再依赖门限）。
- 实测：无信号时 Vpp≈1V（噪声+载波残留），发射时 AFSK Vpp≈0.6–2V。

### 4.6 PA1 的 GPIO 配置

**现象**：接了音频但 `T/P=0`，`L` 恒 0。

**根因**：F103 上定时器输入捕获引脚被配成了 `GPIO_MODE_AF_PP`（复用推挽输出），
外部信号进不了输入路径。

**修复**：改 `GPIO_MODE_INPUT` 后 `L` 能正确跟随偏置。
（后来走 ADC 方案，PA1 改为 `GPIO_MODE_ANALOG`。）

### 4.7 增益与失真、静噪

**现象**：

- 21dB 中频 + 音量 15 + 解调幅度 2：信号严重破音；
- 降到 12dB/音量 12/幅度 1 后不破音，但无信号出现沙沙声。

**原因**：BK4802 的静噪是**基于噪声门限**的：

- 高增益时无信号噪声 EXN≈630，高于开喇叭门限（224×2）→ DSP 保持关喇叭，所以安静；
- 降增益后噪声 EXN≈320，低于门限 → DSP 误判为有信号，把功放打开 → 沙沙声。

**调过的参数**（寄存器定义见 `bk4802.c` / `bbcall_cfg.h`）：

- `reg7 B15:B13`：中频增益，3dB/级（0–21dB）；
- `reg19 B15:B14`：CIC 增益（0/1/3.5/6dB）；
- `reg19 B13:B12`：FM 解调输出幅度；
- `reg19 B03:B00`：接收音量；
- `reg22`/`reg23`：静噪噪声/RSSI 阈值；
- `reg4 B11`：接收音频开关（软件静噪用）。

**当前选择**（为解码保留足够音频摆幅）：

| 项 | 值 | 寄存器结果 |
|---|---|---|
| 中频增益 | 12dB | `reg7=0x8D00` |
| CIC 增益 | 3.5dB | `reg19 B15:14=2` |
| 解调幅度 | 1 | `reg19 B13:12=1` |
| 音量 | 15 | `reg19 B3:0=15` |
| 静噪 | 关闭 | `reg22=0x0C00`、`reg23=0x64FF` |
| 软件静噪 | 关闭 | `BBCALL_SW_SQUELCH=0` |

对应日志：`R19=36879`（0x900F）。

**软件静噪的尝试与结论**：

- 音量 0 压不住 D 类功放底噪，必须用 `reg4 B11=1` 关接收音频通路；
- 第一版用 "RSSI 与 EXN" 双重条件，强信号下 I2C 读 EXN 出错导致一直不放行（发射没声音）；
- 改成只看 RSSI，并在恢复时重锁 reg23，可正常"无信号静音、有信号出声"；
- 最后为了解码测试，暂时把寄存器静噪和软件静噪都关闭（音频常开）。
  后续做产品时再根据实际听感标定阈值。

### 4.8 AFSK 判频方案的三次演进

**第一版：TIM2_CH2 捕获周期**

- 原理：PA1 上升沿测周期，833µs→mark、455µs→space；
- 结果：接 EAROP 原始 D 类 PWM 时 `P` 只有几十 µs；滤波后 `T/P` 仍对不上。

**第二版：去抖 + 音调跳变复位位时钟**

- 加 <250µs 毛刺过滤；检测 mark↔space 跳变复位相位；
- 结果：偶尔能出 `T=002`，但出不了帧。

**根因分析（主机仿真）**：

1. 一个比特的短音调（例如 flag 里的单个 mark）在过零周期法里会整位丢失；
2. `ax25.c` 的 HDLC 移位寄存器按 **MSB-first** 左移，而 AX.25 是 **LSB-first**；
   0x7E 标志因为对称还能认出来，帧体字节却全被位反转，CRC 永远不过；
3. 检测到 0x7E 标志后没有清零移位寄存器，后续字节错位；
4. `tools/gen_afsk_wav.py` 生成的测试音频也是 MSB-first，导致"标准测试音频"本身解不出，
   掩盖了上述问题。

**修复**：

- `ax25.c`：HDLC 改为 `shift = (shift>>1) | (bit<<7)` 的 LSB-first 方式，
  标志检测后清零 `shift`；字节装配同样右移；
- `tools/ax25_reference.py`：位填充/去填充改为 LSB-first；
- `tools/gen_afsk_wav.py`：改为 LSB-first 并重新生成 `tools/test_aprs_144.wav`。

**第三版：ADC 采样 + 8 相位并行解码**

- PA1 改为 `ADC1_IN1`，TIM3 以 **9600Hz**（1200 baud×8）触发采样；
- 每个采样点对最近 8 点做 1200/2200Hz 定点相关（Goertzel），比较幅度判音调；
- 按采样相位分成 **8 路并行 NRZI+HDLC**，每路每 8 个采样得一个比特；
- 只有 CRC 正确的帧才进入邮箱，避免错误相位产生垃圾帧；
- 主机用同一算法对标准音频验证：8 个相位中多个相位能解出 `len=47 CRC=True` 的帧。

### 4.9 解码成功率优化（阈值 + 16 相位 + 跳变位时钟）

**问题**：早期固件一次约 80 秒的日志里只成功解出 2 帧，失败时段特征很明确：

- `OT` 高达 3000~3400（4800 个判决里大部分被当作"幅度太低"丢掉）；
- 成功时段 `OT≈0`、`MK/SP` 都有上千。

**根因**：

1. 相关幅度门限设为 20000，声学耦合/弱信号起伏时大量有效采样被丢弃；
2. 固定 8 相位假设发射端 1200 baud 与 STM32 9600Hz 采样完全同源。实际链路是"手机播放录音 → 手台 MIC → 空中 → BK4802"，播放端音频时钟与 STM32 晶振存在偏差，整包累积后位边界漂移，CRC 必然失败。

**修改**（`firmware-stm32porject/Core/Src/modem.c`）：

- 幅度门限 20000 → **500**：只在极弱时丢弃，弱信号不再被误判为噪声；
- 相位 8 → **16**：用相邻采样插值出半采样相位，覆盖比特边界落在半采样点的情况；
- 新增**跳变对齐位时钟（第三条解码路径）**：检测到 mark↔space 跳变时，把采样点重新对齐到该比特中心，之后按 8 采样/bit 自由运行，下一次跳变再校正，从而跟踪波特率偏差。

**验证**：

- 离线仿真：±2% 波特率偏差、弱幅度、带噪声的条件下仍能解出 CRC 正确帧；
- 实机：新日志 159 行 0.5s 统计中成功解出 **8 个 `[RAW]` / `[FRAME]`**（同一 APRS 包重复发送），`OT=00000`、`MK/SP` 均衡，成功率大幅提升。
### 4.10 成功解码记录

实机（真实 APRS 包，PocketPacket/iPhone 位置报告）收到的串口输出：

```
S=009 MK=02255 SP=02545 OT=00000 A0=01254 A1=02945 M=000

[RAW] len=00091 hex=008200A000B400A00066006400000084008E006A008400980090000E00AE00920088008A00620040000200AE00920088008A006400400003000300F0003D0032003900340035002E00350038004E002F00310032003100330037002E003200360045005B0050006F0063006B00650074005000610063006B00650074002F006900500068006F006E0065002F003100340035002E003000350030004D0048005A002F0041003D00300030003000300032003100A000D0

[FRAME] src=BG5BLH ctrl=003 info==2945.58N/12137.26E[PocketPacket/iPhone/145.050MHZ/A=000021
```

说明：

- 这是一条 APRS **位置报告**（信息域以 `=` 开头），所以只有 `[RAW]/[FRAME]`，没有 `msg=`；
- `[RAW]` 里的十六进制早期用 4 位/字节打印（`0082` 就是 0x82），已改为 2 位/字节；
- 只要出现 `[FRAME] src=...` 就代表 RF→音频→ADC→判频→NRZI→HDLC→AX.25 全链路成功。

APRS 消息类型（信息域以 `:` 开头）会多一行：

```
 msg=Hello APRS 144.640
```

### 4.11 实机联调：LCD 点亮 → 三态 UI → 帧级 RSSI/SNR（2026-09-16）

屏焊上之后逐条排障，这一节是当天的完整记录（现象 → 原因 → 改法）。

| 现象 | 原因 | 改法 |
|---|---|---|
| **背光亮、屏上一个字都没有** | 初始化只发了 `0x2C`：电源控制 `0x28|VC<<2|VR<<1|VF` 只开了电压转换 VC，稳压 VR 与电压跟随 VF 都是关的，V0 建立不起来 | 按 `0x2C → 0x2E → 0x2F` 逐级打开（每级留 2ms），`0x2F` 之后 V0 才到位 |
| **屏全黑（连全白都没有）** | PB3/PB4 复位后是 JTAG 的 JTDO/NJTRST，工程里没关 JTAG，这两个脚不受 GPIO 控制 | `HAL_MspInit()` 里加 `__HAL_AFIO_REMAP_SWJ_NOJTAG()`（关 JTAG-DP、保留 SW-DP，ST-Link 烧录调试不受影响） |
| **文字左右镜像** | 本机模组 SEG 走线是反的 | `lcd_init()` 发 `0xA1`（正装）/ `0xA0`（180° 安装） |
| **画面整体左偏 4 像素** | 模组是 132 列驱动 + 128 列面板，可见 SEG 从芯片内部第 4 列开始 | `lcd_flush()` 列起点 = `bbcall_cfg.h` 的 `LCD_COL_OFFSET`（正装 4，180° 安装 0） |
| **面板要 180° 安装** | 180° = 上下 + 左右一起翻 | `LCD_MOUNT_180=1`：SEG 与 COM 同时反向，列偏移自动换到另一端 |
| 对比度偏浓 | 初始 0x24 偏大 | 改 0x12（`0x81` 后的字节，范围 0x00~0x3F） |
| 焊好后想快速判断屏通不通 | 没有自检手段 | `LCD_BOOT_FLASH=1`：上电全屏点亮 300ms 再清屏；只有硬件侧（PSB/CS/RST/V0/对比度）有问题才看不到这一下 |

UI 与数据侧：

- **收包立即切页**：`ui_feed_ax25()` 入箱后立刻把待机/有未读态切成「有未读」页并重绘（design.md §9 的状态机），不再等 `ui_tick` 的冒号闪烁；正在收件箱里则不抢焦点，只把光标后移一位（新条插队首，看的还是原来那条）。实测：解码一帧后的画面与强制 `--screen unread` **逐字节一致**。
- **新消息点亮背光**：入箱成功（非重复包、非 ackNNN）点亮 15s，否则背光超时后新消息刷了也看不见；`ui_key_event()` 因此改名 `ui_backlight_wake()`。
- **锁屏默认墙钟**：`bbcall_cfg.h` 的 `BBCALL_WALLCLOCK_*`（出厂 20:45 / 周三 9/16），开机即从这一刻走；帧时间戳用同一基准（`BBCALL_WALLCLOCK_BASE_MS + now_ms`），收件箱时间戳才对得上。`BBCALL_WALLCLOCK_ENABLE 0` 退回 `UP HH:MM` 开机时长。
- **呼号带 SSID**：解出 `BG5BLB-12` 时屏幕与串口都写 `BG5BLB-12`（`src[12]`）。
- **帧级 RSSI/SNR**：解码当刻取 BK4802 寄存器 24（低 8 位 RSSI 0..127、bit13:8 SNR 0..63，与串口 `R19=` 同源）随这条消息入箱。**是芯片原始读数，不是标定 dBm**。单次 I2C 读会随机失败（日志里的 `R19=65535` 就是读回 0xFFFF），所以改成每 100ms 采样 + 取接收窗口（1s）峰值：某次读失败只计数、不影响取值，窗口内成功一次就够。
- **UTF-8 截断 bug**：`utf8_clip_tail()` 前几版会把结尾一个**完整**汉字也删掉（位置帧注释末尾丢字），已改成按「尾部这一串字节数够不够一个完整字符」判断。
- **位置帧正文改为注释优先**：经纬度在「有未读」页右上有专用格子，正文再抄一遍会把真正的消息文字挤进滚动区；有注释就只放注释，纯信标才回退成「经纬度 + 类型 + 速度/航向」。
- **无串口时的观测手段**：`s_rf_ok_cnt / s_rf_fail_cnt / s_rf_last_raw`（S-meter 采样成功/失败次数与最近原始值）可以用 ST-Link 的 Live Expressions 直接看，变量名在 GDB 里写 `文件.c::变量名`；`ui_harness.c::s_box[0]` 能看到屏上那条消息的全部字段。**SWO 用不了**：SWO 是 PB3，已经被 LCD 当 SCLK 占用。

内存：v2.0 UI 接进来后 RAM 吃紧，`UI_INBOX_MAX 24→16`、`UI_BODY_MAX 96→64`、`_Min_Stack_Size 0x800→0x600`（详见 design.md §6.3）。当天收尾时 `text=56888 / data=132 / bss=20212`。

---

## 5. 串口诊断字段说明

每 500ms：

```
S=009 MK=xxxx SP=xxxx OT=xx A0=xxxx A1=xxxx M=000
```

| 字段 | 含义 |
|---|---|
| `S` | S 表（RSSI 映射，1–9，最小显示 1） |
| `MK` | 本 0.5s 判为 1200Hz mark 的采样数 |
| `SP` | 判为 2200Hz space 的采样数 |
| `OT` | 幅度过低/无法判别的采样数 |
| `A0/A1` | ADC 原始值的最小/最大值（偏置约 1860，用于判断摆幅/削波） |
| `M` | 软件静噪状态（1=静音，0=放行） |

每 2s：

```
R19=36879 RSSI=00127 SNR=00063 G=006 RX=00012 U=00003 DUP=00005 FIX=00000 FIX2=00000 REP=00000 I2CE=00000 AFC=00047 EXN=00001 R22=0C00 R23=64FF ID=18434 T=001 P=02109
```

| 字段 | 含义 |
|---|---|
| `R19/R22/R23` | BK4802 寄存器回读（增益/静噪配置） |
| `RSSI/SNR/EXN` | 信号强度/信噪比/带外噪声 |
| `AFC` | 剩余频偏 |
| `G` | 自适应中频增益档位（4=12dB、5=15dB、6=18dB） |
| `RX/U/DUP` | 累计接收帧数 / 独立台站数 / 重复帧数 |
| `FIX/FIX2` | 1-bit / 2-bit CRC 纠错成功帧数 |
| `REP` | 参考帧辅助恢复（重复包）成功帧数 |
| `I2CE` | I2C 错误/重试计数 |
| `ID` | 芯片 ID（0x4802=18434） |
| `T` | 最近判出的音调（0=无、1=mark、2=space） |
'| `P` | 最近一次 ADC 原始值（旧版是捕获周期，已改） |

解码事件（`[T=...ms]` 为本次上电后的毫秒时间戳）：

```text
[T=12345ms]
[RAW] len=47 hex=...
[FRAME] src=BG5BLH dest=APRS path=(none) ctrl=3 info=:BG5BLH   :Hello APRS 144.640
 msg=Hello APRS 144.640
[T=12680ms] [DUP] src=BG5BLH
[T=20010ms] [FIX] [RAW] ...        （1/2-bit 纠错成功）
[T=21000ms] [REP] [FRAME] ...      （参考帧辅助恢复）
```

- `[FRAME]`：新解码帧；`[MICE]`/`[POS]`：Mic-E / 普通位置解析；
- `path=...`：中继路径，带 `*` 表示该中继已转发；
- `[DUP]`：60s 内重复帧（短行）；`[FIX]`/`[FIX2]`/`[REP]`：纠错或参考恢复的帧；
- 时间戳来自 `HAL_GetTick()`，复位后从 0 开始，便于把解码事件与手工发射时刻一一对应。

---'

## 6. 主机验证工具

```powershell
# AX.25/APRS 参考实现自测
python tools/ax25_reference.py

# BK4802 频率字
python tools/bk4802_freq.py

# 生成标准 1200 baud APRS 测试音频（LSB-first）
python tools/gen_afsk_wav.py tools/test_aprs_144.wav

# 生成带 VOX 触发的版本（150ms 触发音 + 150ms 保持音 + 数据）
python tools/gen_afsk_wav.py --vox tools/test_aprs_144_vox.wav

# 指定呼号(可带 SSID) + APRS 消息（屏幕上显示成 呼号 + 正文）
python tools/gen_afsk_wav.py --src BG5BLB-12 --addressee BG5BLH ^
    --msg "有内鬼 停止交易" -o tools/test_bg5blb12_msg.wav

# 位置帧：随机经纬度 + 注释文字（未读页会显示经纬度）
python tools/gen_afsk_wav.py --src BG5BLB-12 --pos --random-pos --seed 20260916 ^
    --comment "有内鬼 停止交易" -o tools/test_bg5blb12_pos.wav
```

`tools/test_aprs_144.wav` 的内容是 `APRS → BG5BLH` 的 APRS 消息。实机测试用的是**标准位置帧**：
`tools/test_bg5blb12.wav`（一条包同时带呼号 `BG5BLB-12`、经纬度 `3952.49N/12028.55E`、注释「有内鬼 停止交易」）；
另有两个变体：`test_bg5blb12_msg.wav`（`:` 消息帧，只有正文、不带位置）、`test_bg5blb12_pos.wav`（同位置帧），
都各有一份 `_vox` 版本（前面加 150ms 触发音 + 150ms 保持音）。

> **修过的坑**：`bitstuff_bytes()` 把填充后的比特流按整字节打包，当填充后比特数不是 8 的倍数时，
> 收尾 flag 前会多出 0~7 个 0 位，接收端把它们当成帧内容 -> 帧长错、CRC 失败（位置帧最容易命中，
> 现象是"怎么都解不出"）。已新增位级填充 `bitstuff_bits()` 并改用它；原先那条 `Hello APRS 144.640` 
> 恰好字节对齐，所以这个 bug 一直没暴露。

主机侧用仓库里的 `tools/verify_ui.py` 可以逐像素核对屏幕内容（呼号/经纬度/正文/RSSI/SNR）；
模拟器加了 `--rf R,S`，复现真机"解码当刻取到 RSSI/SNR"的那条路径。

```
:BG5BLH   :Hello APRS 144.640
```

主机的同算法仿真可解出 `len=47, CRC=True`。

---

## 7. STM32CubeIDE 编译与烧录

1. 打开 `firmware-stm32porject/` 工程（用户手动建的 STM32F103C8Tx 工程）。
2. 确认 `Core/Src`、`Core/Inc` 已加入构建；`main.c` 的 USER CODE 区已调用
   `hw_delay_init()/hw_clock_try_72mhz()`、`bbcall_app_init()`、`bbcall_app_loop()`。
3. Build（0 错误即可）。
4. 烧录后打开 USART3（PB10/PB11，115200）看串口输出。

LCD 焊好后把 `bbcall_cfg.h` 的 `BBCALL_LCD_ENABLED` 改成 1 即可启用显示。

---

## 8. 固件功能与审计修复（v0.3）

### 8.1 解码与协议功能

- PA1(ADC1_IN1) 9600Hz 采样 + 1200/2200Hz 定点相关判频；
- 16 相位并行 + 9 条多周期跳变对齐路径（覆盖 ±1.25% 波特率偏差）；
- NRZI + HDLC（LSB-first、去位填充、CRC-16/X.25）；
- 1-bit / 2-bit CRC 纠错（`FIX`/`FIX2`）与参考帧重复包恢复（`REP`）；
- 解码帧 FIFO，长串口打印期间不丢帧；
- Mic-E 位置解码（含目标呼号空格/模糊度处理）与普通 APRS 位置解码（`!`/`=`/`/`/`@`）；
- AX.25 中继路径输出（`path=...`，`*` 表示已转发）；
- 重复包抑制（60s）+ `RX/U/DUP` 统计；
- 自适应中频增益（按 RSSI 分档，`G=4/5/6`）；
- 解码事件毫秒时间戳 `[T=...ms]`；
- 软件静噪（默认关闭；一旦关闭接收音频通路，PA1 也会失去 AFSK，解码停止，需注意）。

### 8.2 审计修复

- 栈安全：`bbcall_app_loop` 栈 1120→80B，`modem_adc_sample` 416→88B，大缓冲改静态；`_Min_Stack_Size=0x600`（v2.0 UI 实测最大单帧 576→320B）；
- 采样计数器 Q8 时间改为 32 位回绕安全比较，避免约 14.6 分钟后溢出；
- AX.25 解码增加 `idx+4 > len` 边界检查，避免 `info_len` 下溢越界；
- ADC 中断等待加 2000 次超时，超时丢弃本次采样，避免死等；
- `AX25_MAX_FRAME 330→256`；LCD 三态界面接进来之后 RAM 已经吃满（见下方 v2.0 条目），收件箱容量按 RAM 反推为 16 条；
- 启用 IWDG 2 秒看门狗（调试暂停时冻结）；
- I2C 增加 ACK 校验、3 次重试、9 脉冲总线恢复与 `I2CE` 计数；
- 参考帧恢复阈值 8→4 bit，降低误恢复风险；
- 清理废弃的 TIM2 捕获路径与旧 modem 接口。

### 8.3 最新实测（SunSDR2 DX + 手动 MOX，低功率）

- 10 次手动发射，**9 次成功解码（90%）**；
- 解码时间戳簇：8947ms、10957ms、12773ms、16074ms、17791ms、19667ms、21363ms、23441ms、25240ms（每簇 3 次并行解码）；
- 解码时 RSSI 89–90 / SNR 38–39 / EXN 27–31，`FIX/FIX2/REP=0`、`I2CE=0`、`G=6`（18dB）；
- 唯一疑似漏解出现在第 3、4 次之间（间隔 3.3s，其他为 1.7–2.1s），属发射侧 MOX 时序/音频问题，不是接收灵敏度；
- 对比：VOX 直发只有 4/10，手动 MOX 9/10，说明 VOX 时序是主要瓶颈；VOX 测试音频已增强为 150ms 触发 + 150ms 保持。
## 9. 硬件改进方案（提升解码率）

当前硬件：天线直接短接到 BK4802 的 ANT 脚，**没有隔直、滤波和 50Ω 匹配**。这会导致灵敏度下降、FM 广播/409MHz 玩具机等强带外信号阻塞前端、底噪升高（EXN 偏大），是当前弱信号解码率的主要瓶颈。按收益排序如下。

### 9.1 射频前端（收益最大）

目标结构：

```text
天线(50Ω) → ESD保护 → 100pF隔直 → 144–146MHz带通 → [可选LNA] → π/L匹配 → 100pF隔直 → BK4802 ANT
```

- **隔直/保护**：ANT 脚串 100pF C0G（数据手册典型 C1=100pF）；天线端加 ESD 二极管/阵列（BAT54S/BAV99 等）；RF 走线按 50Ω 设计，短而直，两侧打地过孔。
- **2m 带通**：优先从坏掉的 2m 手台/接收机拆前端带通或螺旋滤波器，或购买 144–146MHz 带通模块；要求插损 ≤2–3dB，对 88–108MHz、409MHz 抑制 ≥30dB。自建 LC 带通：中心 144.64MHz、带宽 3–5MHz、3 阶；并联谐振起始值约 L=82–100nH、C=12–15pF，耦合 2–5pF，用 NanoVNA 调到 S11<-10dB、插损<2dB。
- **与 BK4802 匹配**：BPF 与 ANT 脚之间加 π 型或 L 型匹配（串 L + 两端并 C）；无 VNA 时用弱信号源调 L/C，使 RSSI/SNR 最大、EXN 最小，目标弱信号 RSSI 提升 6dB 以上。
- **可选 LNA**：BPF 之后加 PGA-103+ / SPF5189Z（NF≈0.5dB）；强信号环境在 LNA 前加 3–6dB 衰减器或 BAP64-02 PIN 限幅器，避免 LNA 饱和。

### 9.2 音频链路（EAROP → PA1）

- 单级 RC 升级为两级 RC 或 LC 低通，彻底滤掉 D 类 ~100kHz 载波，同时保留 1200/2200Hz；
- 加比较器整形（LM393/TLV3501，阈值 1.65V + 迟滞）或轨到轨运放带通（MCP6002/TL072，中心约 1.7kHz、增益 5–10 倍）；
- 音频线用屏蔽线/双绞线，尽量短、远离 RF 和数字线；PA1 对地并 100pF 抑制 RF；
- 耦合 1µF、偏置 100k/100k，静态 1.65V，摆幅 0.7–1.5Vpp，避免削顶。

### 9.3 电源、接地与屏蔽

- MCU 与 BK4802 用低噪声 LDO（TPS7A4700/LT3045，或 AMS1117 + 100µF+10µF+100nF），优先电池/线性电源，少用 USB 供电；
- BK4802 的 VCCRF/VCCIF/VCCAUD/VDDVCO 分别去耦，VSSAUD 单独星型接地；
- 电源线串磁珠/共模扼流圈；数字地与射频地在电源入口单点相连；
- 射频部分加金属屏蔽罩，天线座就近接地，MCU/晶振/串口线远离 RF 前端。

### 9.4 晶振与频率精度

- 21.25MHz 晶振换 ±10ppm 以内 TCXO/高精度晶振，负载电容按手册；走线短、包地；
- 保持 AFC 开启（当前 reg20 默认开），弱信号下频偏会吃掉解码余量。

### 9.5 强信号与衰减

- 近场/强台：加 3/6/10dB 衰减器或步进衰减器，避免 BK4802 前端/中频饱和；
- 弱台：BPF + LNA；LNA 必须在 BPF 之后，并加限幅保护。

### 9.6 发射端（SunSDR2 DX）

- 音频线或手动 MOX，避免 VOX 时序丢前导；FM 频偏约 3kHz，关闭 ALC/压缩/EQ，音频峰值约 -6dB；
- 发射天线/馈线调好 SWR；近场测试用假负载+衰减器，别让接收端过载。

### 9.7 验证与测量

- NanoVNA：调天线/BPF/匹配（S11<-10dB，插损<2dB）；
- SDR/频谱仪：看 2m 附近是否有强带外信号（FM 广播、寻呼、409MHz 玩具机）；
- 步进衰减器：测灵敏度与解码率，记录 `RSSI/EXN/RX/U/DUP/G/OT` 对比改动前后。
## 10. 待办 / 下一步

BB 机功能规划详见 [PLAN.md](PLAN.md)，按版本推进：

### v0.4（MVP：能当 BB 机用）
- [ ] ST7567 显示呼号/时间/正文/未读数
- [ ] 蜂鸣/振动提示与静音模式
- [ ] 收件箱/消息详情/删除、未读计数
- [ ] RSSI/SNR/电池状态页

### v0.5（存储与菜单）
- [ ] W25Q64/AT24C512 消息与设置存储
- [ ] RTC（DS3231/内部 RTC）时间戳
- [ ] 菜单/设置、台站列表、按键锁、重复提醒

### v0.6（APRS 扩展）
- [ ] 群发/公告、Ack 显示、关键词过滤
- [ ] 天气/遥测/状态包解析
- [ ] 位置/方位显示

### v1.0（可选双向，需发射）
- [ ] BK4802 发射通路、Ack/Rej、发送消息

### 其他
- [ ] 静噪最终标定（当前为解码测试关闭）
- [ ] 射频前端硬件改进（见第 9 节）
- [ ] 清理 `firmware-stm32porject/PORT.md` 过时说明

---

## 11. 许可与合规

### 本项目

- 许可证：**GNU General Public License v3.0（GPL-3.0）**，见根目录 `LICENSE`。
- 代码与文档如无特别说明，均按 GPL-3.0 分发。

### 参考项目许可证与兼容性

| 项目 | 许可证 | 与本项目 GPL-3.0 是否兼容 | 说明 |
|---|---|---|---|
| [MM-Radio](https://github.com/doublehan07/MM-Radio) | **BSD-2-Clause** (c) 2024 Han Zhang | 兼容 | 主参考/工程底座；保留其版权与许可声明 |
| [BG5ESN FMO-Radio-Module-BK4802-V2.00](https://github.com/BG5ESN/FMO-Radio-Module-BK4802-V2.00) | **MIT** (c) 2025 BG5ESN | 兼容 | 频率字计算参考 |
| [VP-Digi](https://github.com/sq8vps/vp-digi) | **GPL-3.0** | 兼容 | AFSK/AX.25/APRS 资料参考 |
| [BG7QKU STM32_SIMPLE_CONTROL_BK4802N](https://github.com/BG7QKU/STM32_SIMPLE_CONTROL_BK4802N) | **未声明 LICENSE**（默认保留所有权利） | 不可直接复制代码 | 仅作资料参考；引用代码需作者授权 |
| STM32 HAL / CMSIS（`Drivers/`） | ST 工程自带许可（目录内 `LICENSE.txt`） | 兼容（保留声明） | CubeIDE 生成代码，勿删许可文件 |

许可证原文放在 `licenses/`，第三方组件说明见 `THIRD_PARTY_NOTICES.md`。

### 合规要点

1. **BSD-2 / MIT 代码并入 GPL-3.0 是允许的**，但要保留原版权声明、许可全文和免责声明。
2. 发布 HEX/BIN/Release 时，二进制分发同样需要附带 `LICENSE`、`licenses/` 与 `THIRD_PARTY_NOTICES.md`（或在 Release 说明中给出链接）。
3. **不要直接复制 BG7QKU 仓库的代码**：该仓库未声明 LICENSE，默认保留所有权利。
4. `BK4802P.pdf` 作为 BK4802P 参考数据手册保留在仓库中；版权归 Beken 所有，仅供学习与开发参考。
5. 本项目只做接收（RX-only）。
## 12. 参考项目

- [MM-Radio](https://github.com/doublehan07/MM-Radio)
- [BG7QKU STM32_SIMPLE_CONTROL_BK4802N](https://github.com/BG7QKU)
- [BG5ESN FMO BK4802 V2.00](https://github.com/BG5ESN/FMO-Radio-Module-BK4802-V2.00)
- [VP-Digi](https://github.com/sq8vps/vp-digi)

## 13. PC 端 LCD 模拟器（SDL2）

用 SDL2 在 PC 上模拟 ST7567 128×64 单色点阵，直接编译固件里的 `lcd_st7567.c` 绘图代码，无需烧录即可看屏幕效果。

- 代码目录：`simulator/`（CMake + `src/lcd_sim.c` + `src/ui_harness.c` + `src/main.c`）；
- 复用固件代码：`lcd_st7567.c`、`font8x16.h`（`LCD_SIM` 条件分支），绘图逻辑与真机一致；
- 按键：↑/↓ 上下、Enter 确定、Backspace 返回、T 测试图案、M 消息、S 待机、I 反显、B 背光、F12 截图、Esc 退出；
- 无窗口自检：`bbcall_sim --selftest`，生成 `sim_selftest.bmp`。

### 13.1 免安装构建（Windows，已在本机跑通）

本机没有任何 x86 编译器 / CMake / MSYS2，因此模拟器改用**仓库自带的 TinyCC + 内置 SDL2** 构建，不需要额外安装任何东西：

```powershell
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1            # 只编译
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Selftest  # 编译 + 无窗口自检
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Run       # 编译 + 打开窗口
```

- TinyCC：`third_party/tcc/`（本地免安装工具链，`.gitignore` 已排除）；
- SDL2 2.32.10（x86_64-w64-mingw32）：`third_party/sdl2/`，含 `include/SDL2`、`bin/SDL2.dll`、`lib/libSDL2.dll.a` 与 zlib 许可 `LICENSE.txt`；
- 产物：`simulator/build-win/bbcall_sim.exe`（脚本会把 `SDL2.dll` 一并拷到该目录）。

构建过程中踩到并已解决的两个坑（脚本里已处理）：

| 现象 | 原因 | 处理 |
|---|---|---|
| `SDL_platform.h:265: error: ';' expected (got "SDL_GetPlatform")` | TCC(x86_64) 把 `__cdecl` 当普通标识符；SDL `begin_code.h` 在 `__WIN32__ && !__GNUC__` 时把 `SDLCALL` 展开成 `__cdecl` | 编译时加 `-D__cdecl=` |
| `libSDL2.dll.a: error: invalid object file` | TCC 的链接器解析不了新版 MinGW 生成的 GNU 导入库 | 直接链接 `bin/SDL2.dll`（TCC 会读 DLL 导出表） |
| `could not write 'bbcall_sim.exe': Permission denied` | Windows 会锁定正在运行的 exe | 先退出模拟器窗口（Esc）再重新构建 |

### 13.2 其它构建方式

`simulator/CMakeLists.txt` 与 `simulator/Makefile` 仍然保留，供已装 MSYS2 / vcpkg / w64devkit 的机器使用，命令见 `simulator/README.md`。

### 13.3 渲染验收

`--selftest` 写出 BMP（默认 512×256，即 4 倍放大）。实测图案页正确显示 4×4 棋盘、两条对角线，
以及反白文字 `ST7567 SIM` / `128x64 LCD`；详情页与 `draw_detail()` 的期望绘制**逐像素差异为 0**。

**列偏移：本机模组要右移 4 像素。**
**安装方向：本机面板是 180° 安装。** 180° = 上下 + 左右一起翻，所以 SEG 与 COM 同时反向：
`lcd_init()` 发 `0xA0` + `0xC8`（正装是 `0xA1` + `0xC0`），列起点也随 ADC 方向换到另一端
（`LCD_COL_OFFSET` 变成 0）。开关是 `bbcall_cfg.h` 里的 `LCD_MOUNT_180`（1 = 180° 安装）。
模拟器按同一安装方向建模，预览就是用户实际看到的方向。

**列偏移：本机模组 132 列驱动 / 128 列面板。** 模组是 132 列驱动 + 128 列面板，可见 SEG 从芯片内部第 4 列开始，
按常规从第 0 列写会让画面整体左偏 4 像素。`lcd_flush()` 现在从 `LCD_COL_OFFSET`（`bbcall_cfg.h`，默认 4）
指定的列开始写，模拟器按同一块屏建模（列地址 0..131，可见列 = 芯片列 - 偏移），所以预览画面不变。

**屏幕方向：实板结论是 SEG 反向。** 本机 LCD 模组的 SEG 走线是反的：发 `0xA0`+`0xC0` 时实板整屏
左右镜像，改成 **`0xA1` + `0xC0`** 后实板正常（屏焊上后实测）。模拟器按同一块屏建模
（`lcd_sim.c` 的 `s_panel_flip = 1`），两边相消后预览与实机一致，`--selftest` 的期望画面不变：

```text
y0   |BBCALL APRS RX  |      y0   |MSG BG5BLH      |
y16  |144.640 MHz     |      y16  |Hello APRS      |
y32  |RX=13 MSG=13    |      y32  |144.640         |
y48  |BD4BE  POS      |      y48  |1/1             |
```

常见 ST7567 模板是 `0xA1`+`0xC8`（两者成对反向）或 `0xA0`+`0xC0`（都正常）；
`0xA1`+`0xC0` 只剩左右镜像，正好抵消这块模组的反接。对比度是 `lcd_init()` 里的 `0x81` 参数
（当前 0x12，范围 0x00~0x3F）。模拟器里按 `F3` 可切换对比两种朝向。

### 13.4 三种数据源（S2 + S3）

| 来源 | 命令 | 经过的固件代码 |
|---|---|---|
| 内置示例 | `--demo` | `ax25.c` + `aprs.c` |
| 串口日志回放 | `--replay tools\sample_aprs_log.txt` | `[RAW] hex=` → `ax25.c` + `aprs.c` |
| WAV 音频解调 | `--wav tools\test_aprs_144.wav` | `modem.c` → `ax25.c` → `aprs.c` |

WAV 路径就是**完整的固件解码链路**：48kHz/16bit PCM 经 5 点滑动平均降到 9600Hz，映射成 12bit ADC 码值
（中心 2048、幅度 ±800）后逐点喂给固件入口 `modem_adc_sample()`，再用 `modem_get_frame()` 取帧——
相当于把 STM32 的 ADC 中断源换成音频文件。实测 `test_aprs_144.wav` 解出 1 帧（另有 15 次重复抑制，
来自 16 路并行相位走廊各解一遍，与固件 `bbcall_app.c` 的去重行为一致）。

收件箱数据模型：`M` 消息 / `P` 位置 / `C` Mic-E / `X` 其它，支持上下选择、Enter 打开、Delete 删除、
详情分页，右下角标注 `FIX`/`REP`/`RELAY`。用真实 5km 接收日志回放，13 个帧全部入箱，
呼号、类型与 `tools/ax25_reference.py` 独立解码结果完全一致：

```text
y0   |INBOX 13/13     |
y16  | BH4FSK C#11    |
y32  | BD4SDX P#12    |
y48  |>BD4BE  C#13    |
--- 详情（Mic-E）---
y0   |POS BD4BE       |
y16  |3111.28N        |
y32  |12125.77E M0:   |
y48  |1/2    RELAY    |
```

### 13.4.1 中文字库（16x16 子集）

> **已被取代（2026-09-16）**：UI v2.0 重设计后，中文显示改用 **Fusion Pixel 12px/10px 字模**
> （`tools/gen_fusion_font.py` 生成 `firmware-stm32porject/Core/Inc/fusion_font.h`，374 字形，规格见
> [design.md](design.md) §3.5）。下面的 GNU Unifont 16x16 子集方案与 `cn_font_data.h` /
> `CN_FONT_ENABLED` 链路保留作历史记录，新 UI（`ui_harness.c`）不再编译 `cn_font.c`，
> `build_win.ps1` 也不再自动检测该头文件。

> **阶段说明（重要）**：本轮只做到「**字库链路**」为止——生成器、查找、绘制、自检样张都已就绪并验证。
> **界面文案的汉化（把 HOME/MENU/INBOX 等换成中文）明确排到后续阶段执行**，不在当前范围内；
> 后续阶段的排期与验收见 [PLAN.md](PLAN.md) 第 6 节 v0.7 与第 8.5 节。
> 这样分步是因为：字库是基础设施（要提前验证体积与许可），而文案替换要等界面结构稳定后再做，
> 否则每改一次版面就要重排一次中文行宽。

界面文案目前是 ASCII。中文字库链路已打通，方案参考
[EthanYan6/Dondji](https://github.com/EthanYan6/Dondji)（Apache-2.0：菜单汉化 + 中文输入法 + 中文信道名）：

- **布局沿用它的形状**：`[位图][Unicode 索引 4B/项 升序][拼音表][版本字节]`。
  Dondji 把字库放**外部 SPI Flash**、固件只留布局常量；我们暂时只做**片上子集**，
  保持同一形状是为了将来接外部 Flash、加拼音表时读取逻辑不用改。
- **字源换成 GNU Unifont**：Dondji 用 WenQuanYi Bitmap Song，而它是 **GPL v2 only + 字体嵌入例外**，
  与本项目 GPL-3.0 不兼容；Unifont 自 2013 起是 **GPLv2+ / OFL-1.1 双许可**，且本身就是 16x16 点阵。
- **预算**：每字 36 字节（位图 32 + 索引 4）。当前子集 107 字，目标平台实测占 **4,028 字节** Flash；
  片上约可放 1055 字；全 GB2312（6763 字）需约 243KB，必须外置 SPI Flash。
- 工具：`tools/gen_cn_font.py`（字符清单 `tools/cn_chars.txt`）；`CN_FONT_ENABLED` 控制启用，
  模拟器检测到 `cn_font_data.h` 会自动打开；`--screen cnfont` 可逐页检查字形，
  About 页显示 `CN FONT <字数>`。

```powershell
python tools\gen_cn_font.py --unifont <unifont.hex> --chars-file tools\cn_chars.txt `
    --out-header firmware-stm32porject\Core\Inc\cn_font_data.h --out-bin tools\cn_font.bin
```

> 重新生成字库后**必须同步布局常量**。Dondji 文档记录过这个坑：只刷新字库 bin 而没改拼音表偏移，
> 结果是"任意拼音候选错乱、大量音节失败"，不是个别字问题而是整表错位。

### 13.4.2 ASCII 字模：等间距、点阵源与生成方式

> **现状（2026-09-16）**：UI v2.0 的三态界面**只用 Fusion Pixel 字模**（见 design.md §3.5），
> 下列 `gen_font.py` 8×16/6×8 字模不再参与新界面渲染；按 design.md §3 规定，与 Fusion Pixel
> 混用**不允许**，`gen_font.py` 仅保留作纯 ASCII 兜底字模的生成工具。本小节其余内容作历史记录保留。

两套 ASCII 字模都由 `tools/gen_font.py` 生成，**是等间距的**：

- 渲染器不管字形宽窄，一律固定步进：`lcd_draw_string8x16()` 每字 +8px、`lcd_draw_string6x8()` 每字 +6px，
  所以屏幕上必然是等间距网格；
- 字模本身来自**等宽的像素点阵字体**（见下表）。

| 用途 | 源 | 单元格 | 说明 |
|---|---|---|---|
| 小字号 | `tools/bdf/6x9.bdf` | 6x8 | 字形用满 6 列；基线取 6，大写落在行 1..6 |
| 大字号 | `tools/bdf/7x13.bdf` | 8x16 | 7 列字形 + 8px 步进，留 1px 字距 |

两者都是 **X11 misc-fixed** 家族，BDF 内自带 `COPYRIGHT "Public domain font. Share and enjoy."`，
**公有领域**，可自由分发（见 `THIRD_PARTY_NOTICES.md` 与 `tools/bdf/README.md`）。

```powershell
python tools\gen_font.py --bdf tools\bdf\6x9.bdf  --w 6 --h 8  --baseline 6 `
    --name font6x8  --macro FONT6X8_H  --out firmware-stm32porject\Core\Inc\font6x8.h
python tools\gen_font.py --bdf tools\bdf\7x13.bdf --w 8 --h 16 `
    --name font8x16 --macro FONT8X16_H --out firmware-stm32porject\Core\Inc\font8x16.h
```

**为什么不再用 TrueType 栅格化**：小尺寸笔画常落在半个像素上，阈值一卡就整条竖笔消失。
实测 Consolas 9px 的 `M` 两条竖线灰度只有 135/141 与 163/**121**，阈值 128 时右侧那条被吃掉，
同一批里 `H` 只剩一竖、`K` 几乎空白。改用公有领域点阵 BDF 后，`M W H K N L` 都是标准形状。

参考做法来自 [joaquimorg/UV-KX](https://github.com/joaquimorg/UV-KX)（BDF + u8g2 的 bdfconv 转紧凑数组）。
注意该仓库**未声明许可**，所以只借鉴做法，未使用其代码或其字源（Pixies / Uni0553 版权归个人）。

> 生成器会报告缺字与空白字形，并在整字落到单元格外时退化为贴底放置（下划线 `_` 即属此类）。
> 改字体或尺寸后必须重跑并抽查 `M W H K N L` 与降部 `g j p q y`。
### 13.5 UI 重设计：复古寻呼机（BB 机）风格

**导航结构（v2.0，2026-09-16）**：整个 UI 只有**三个屏幕态**——待机 / 有未读 / 收件箱，
坐标、字模、按键状态机全部锁死在 [design.md](design.md)（§5 骨架 / §6 三态 / §8 硬规则），
该文件是唯一权威规范（原 `UISkill.md` 已并入其中，文件本体已删除，历史版本见 git）。

```text
无未读 → 待机态     大格恒反显：时钟(2x) + 日期/开机时长；右半三小格：本机 / 电量 / 未读
有未读 → 有未读态   大格恒反显：最新未读的发件人+正文前两行+时刻；右半：APRS / - / RSSI / SNR
● 短按 → 收件箱态   顶栏反显（发件人 + n/N）；正文两行；元信息两行（时刻 RSSI / 路径 CRC）
▲▼ 滚动（先滚正文再翻条）；● 短按标已读并前进；● 长按 620ms 退出；未读清零自动回待机
```

按键只有 ▲ ▼ ● 三个（● 长按 620ms = 退出/返回最外层），与真机一致。

**历史沿革**：v2.0（2026-09-14）曾把 HEARD 台站列表与 MESSAGES 消息列表并存，
实测让用户困惑（两套收件箱），合并为统一收件箱；ackNNN 送达确认只计数不进收件箱。
磁贴方案（G 息屏 / G2c 收件箱，预览见 `ui_previews/G*.png`）的墨量分层、层级可辨等原则
以规则形式并入 design.md §4/§5，版面本身废弃。再早的 boot/home/menu/detail/radio/about
七屏 chrome 系统（反显状态栏 + 6 行 6x8 网格）已被三态模型整体取代，仅作历史记录保留。

- **Design Read**：90 年代末点阵寻呼机（Motorola Advisor 一类）视觉语言；
  `DESIGN_VARIANCE 6 / MOTION_INTENSITY 2 / VISUAL_DENSITY 7`
  （密度 7 -> 用 1px 细线分隔数据、不用卡片盒；运动 2 -> 只保留大时钟冒号闪烁）。
- **字体**：**Fusion Pixel 12px（正文/数值）+ 10px（标签）**，374 字形（95 ASCII + 279 汉字），
  由 `tools/gen_fusion_font.py` 从原型 `bbcall-aprs-screen-states.html` 内嵌字表提取，
  生成 `firmware-stm32porject/Core/Inc/fusion_font.h`；与旧 `gen_font.py` ASCII 字模混用不允许（design.md §3）。
- 界面状态机在 `firmware-stm32porject/Core/Src/ui_harness.c`（三态，模拟器与真机单源共用）；绘图原语（`lcd_fill_rect` / `lcd_hline` /
  `lcd_vline` / 反显填充 / Fusion Pixel 字模绘制）在固件 `lcd_st7567.c`，**真机与模拟器同一份代码**。
- 数据全部诚实显示、无采样就留白（design.md §12）；锁屏默认墙钟由 `bbcall_cfg.h` 的 `BBCALL_WALLCLOCK_*` 给出
  （出厂值 **20:45 / 周三 9/16**，`BBCALL_WALLCLOCK_ENABLE 0` 则退回 `UP HH:MM` 开机时长）；
  本板无电池采样电路，电量位显示 `--`；真机 RSSI/SNR 取解码当刻的 BK4802 寄存器 24 原始读数（低 8 位 RSSI、bit13:8 SNR，与串口 `R19=` 同源），随帧存进条目一起显示，读失败显示 `--`（原始读数，非 dBm）；模拟器只在 `--demo` 里注入已标定样例值，日志回放的未标定
  原始寄存器值不注入，显示 `--`。

消息界面的数据规则（最新在上、`NOW`/`12m`/`3h` 年龄列、未读 `*`、60s 重复包抑制）参考
[GOGUFW-UV-K1-Messenger](https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger)
（Apache-2.0，同样是 128x64 单色 LCD 的对讲机固件）。取舍逐条记在 [design.md](design.md) §11。
当前屏幕：`idle`（待机）/ `unread`（有未读）/ `inbox`（收件箱）/ `pattern`（点阵样张）；
设置与诊断类二级页本规范尚未覆盖（design.md §12 留白）。

**验收**（三态模型，2026-09-16）：`tools/verify_ui.py` 对 `idle / unread / inbox / inbox2`
四张截图做逐像素校验（反显底填充、挖字极性、坐标、分隔线），**全部通过、差异为 0**；
`--keymap` 键盘映射自检 6 项全过；`build_win.ps1 -Selftest` 编译 + 固定参数自检通过；
13 帧真实日志回放注入 13 条消息正常。截图：`simulator/build-win/v3_idle.png` /
`v3_unread.png` / `v3_inbox.png`。

> **固件移植（2026-09-16 已落地，待真机烧录验证）**：三态 UI 已移入固件
> `Core/Src/ui_harness.c`（模拟器与真机**单源共用**，模拟器构建直接编译固件目录这份），
> `bbcall_app.c` 完成接线（按键 PB12/13/14、背光 PB0 按 15s、喂帧、时钟），
> 由 `bbcall_cfg.h` 的 `BBCALL_LCD_ENABLED`（默认 1）与 `BBCALL_MYCALL` 控制。
> 移植细节与三条实现补全见 [design.md](design.md) §12.4；
> CubeIDE 里需 **F5 刷新工程**让新文件进构建。