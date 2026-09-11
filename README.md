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
```

`tools/test_aprs_144.wav` 的内容是 `APRS → BG5BLH` 的 APRS 消息：

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

- 栈安全：`bbcall_app_loop` 栈 1120→80B，`modem_adc_sample` 416→88B，大缓冲改静态；`_Min_Stack_Size=0x800`；
- 采样计数器 Q8 时间改为 32 位回绕安全比较，避免约 14.6 分钟后溢出；
- AX.25 解码增加 `idx+4 > len` 边界检查，避免 `info_len` 下溢越界；
- ADC 中断等待加 2000 次超时，超时丢弃本次采样，避免死等；
- `AX25_MAX_FRAME 330→256`，RAM 余量约 4.3KB，为 LCD 留空间；
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

**已修复：屏幕水平翻转。** 固件 `lcd_init()` 原先发送 `0xA1`（SEG/ADC 段反向），配合 `0xC0`（COM 正常）
会让整屏左右镜像，文字全部反着显示。现改为 `0xA0` + `0xC0`，画面正常。模拟器按状态机忠实复现，实测：

```text
y0   |BBCALL APRS RX  |      y0   |MSG BG5BLH      |
y16  |144.640 MHz     |      y16  |Hello APRS      |
y32  |RX=13 MSG=13    |      y32  |144.640         |
y48  |BD4BE  POS      |      y48  |1/1             |
```

常见 ST7567 模板是 `0xA1`+`0xC8`（两者成对反向）或 `0xA0`+`0xC0`（都正常）；
`0xA1`+`0xC0` 这种混搭正好只剩左右镜像。模拟器里按 `F3` 仍可切换对比两种朝向。

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

### 13.5 UI 重设计：复古寻呼机（BB 机）风格

原来只有 8x16 一套字体，每屏 4 行 x 16 字符，列表只有呼号、详情只有 2 行正文，视觉上更像通用调试界面。
现在按 `ui-design` + `taste-skill` 的 overlay 契约重做。

**先说适用范围（诚实优先）**：这两个 skill 的目标产物是 HTML/CSS/JS。按 taste 第 13 节「OUT OF SCOPE」
与 ui-design 第 1 节（Track A/B 均为 Web），**工程半边不适用于 128x64 1-bit LCD**：没有组件库、
没有 WCAG、没有 Core Web Vitals、没有 GSAP。这里只执行 taste 的三块内容，并把 Web 工程护栏换成嵌入式等价物：

| Web 原规则 | 128x64 1-bit LCD 的等价物 |
|---|---|
| 颜色对比 / 单一强调色 | 只有开与关两态，层级靠**面积、墨量、反显**；「强调色」只能是反显 |
| `prefers-reduced-motion` | **刷新成本与闪烁**：只重绘必要区域，不做全屏闪 |
| Dark mode | 屏的两种极性：**背光正显 / 反显负片**，两者都设计 |
| Breakpoints / 移动端折叠 | 固定 128x64 网格；密度档 = 6x8 与 8x16 |
| 禁止手搓 SVG 图标 | 必须自绘 1-bit 图标，等价纪律：**统一 8x8 网格、统一 1px 线宽、单一图标家族** |

**Design Read**：复古手持寻呼机设备 UI，用户是单人业余无线电操作者，采用 90 年代末点阵寻呼机
（Motorola Advisor 一类）的视觉语言，倾向 1-bit 单色系统：状态图标栏 + 反显选择条 + 大字号时钟。

**Design Dials**：`DESIGN_VARIANCE 6 / MOTION_INTENSITY 2 / VISUAL_DENSITY 7`。密度 7 按 taste 第 7 节
要求**用 1px 细线分隔数据、不用卡片盒**；运动强度 2 只保留一处有动机的动画（大时钟冒号闪烁，
作为设备存活反馈）。

**Anti-default**：1-bit 世界最偷懒的默认是「什么都套一个 1px 方框」（等价于 Web 的卡片默认）。
本项目改用**单一 chrome 系统**，全屏只此一套：

```text
y0..7    顶部状态栏（反显）：左 屏幕名 | 右 [信号格] 3px [静音] 3px [未读数] 3px [信封]
y8       1px 细线
y10..56  内容区，6x8 行网格 y = 10 / 18 / 26 / 34 / 42 / 50
x124..127 滚动轨（仅列表溢出时出现）
```

选择态一律用**反显条**，不加箭头、不换色。

**新增 6x8 字体**（`font6x8.h`，由 `tools/gen_font.py --small` 生成，**固件与模拟器共用**），
每行 21 字符；驱动新增 `lcd_hline` / `lcd_vline` / `lcd_rect` / `lcd_fill_rect` 与 2 倍放大绘制
（8x16 放大成 16x32 用作大时钟）。全部仍在 `lcd_st7567.c`，真机与模拟器同一份代码。

| 屏幕 | 内容 |
|---|---|
| `boot` | 2 倍放大 "BBCALL" + 副标题 + 固件版本 |
| `home` | 状态栏 + **16x32 大时钟** + 频率/RX 数 + 最近一条来源与位置 |
| `menu` | 6 项菜单：8x8 图标 + 名称 + 右对齐数值，选中行整行反显 |
| `inbox` | 6 行，**最新在上**，每行 呼号 + 类型 + 摘要 + 未读 `*` |
| `detail` | 标题 + 5 行正文 + 页脚（页码 / `RELAY <中继路径>` / `FIX` / `REP`） |
| `radio` | 频率 + S 表 + RSSI / SNR / AFC / EXN + RX / DUP + 音频状态 |
| `about` | 版本与硬件信息 |
| `confirm` | 删除确认：填充块 + 内嵌 1px 框 + 反显文字 |

图标家族统一 8x8、1px 线宽：信号格（4 柱高度 3/5/7/8，按 S 表点亮）、信封、静音喇叭、
地图针、天线、对比度、信息 i、电源。

用真实 5km 日志回放（`tools/sample_aprs_log.txt` 已补齐每帧之前最近的 `S=` 与 `R19=` 真实状态行）：

```text
== HOME ==                            == MENU ==
rail | HOME|                          rail | MENU|
clk  |01 12|   (16x32 放大)           y10  |IJInbox           13|  <-反显(选中)
y46  |144.640 MHz     RX 13|          y18  |  Positions|
y54  |BD4BE  MIC-E  3111.28|          y26  |  Radio|
                                      y34  ||gContrast|
== INBOX ==                           y42  |  Backlight|
rail | INBOX 1/13|                    y50  |  About|
y10  |BD4BE  C 3111.28N 1*|  <-反显
y18  |BD4SDX P 3054.31N 1*|            == RADIO ==
y26  |BH4FSK C 3037.90N 1*|            rail | RADIO|
y34  |BG4AIZ C 3137.15N 1*|            y10  |144.640 MHz  S4|
y42  |BD4BE  C 3111.28N 1*|            y18  |RSSI 72    SNR 19|
y50  |BI4BKX C 3116.01N 1*|            y26  |AFC  14    EXN 315|
```

（表中 `IJ`、`|g` 是读屏工具把 8x8 图标当字形匹配的结果，不是画面内容。）

**已知留白（诚实标注）**：电池图标位保留但未启用，本板没有电池采样电路，与其画一个假电量不如留空；
大时钟是**开机计时**而非墙上时钟（无 RTC），左侧 `UP` 即为此意；`Contrast` 菜单项在真机上会改
ST7567 `0x81` 的值，模拟器里是空操作。

> 固件 `bbcall_app.c` 自己的 LCD 画面还是旧的简单版（`BBCALL_LCD_ENABLED=0`，休眠中）。
> 等 LCD 焊上以后，把 `simulator/src/ui_harness.c` 的这套版面移植过去即可。