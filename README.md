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
- [许可与合规](#9-许可与合规)

用 **BK4802P（玩具对讲 FM 收发芯片）+ STM32F103C8T6 + ST7567 12864 LCD**
复刻一台 APRS 寻呼机（BB 机）。仅接收（RX-only），默认频率 **144.640MHz**，
目标是把空中收到的 APRS 数据包解出来并显示在 LCD 上。

当前状态：**RF → 音频 → ADC → 判频 → NRZI → HDLC → AX.25 → APRS 全链路已打通**，
已用实机收到并解析真实 APRS 数据包（见下文「成功解码记录」）；经解码算法优化（幅度门限 20000→500、16 相位、跳变对齐位时钟）后成功率大幅提升。LCD 尚未焊接，
当前通过 USART3（PB10/PB11，115200）输出调试信息。


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
R19=36879 RSSI=00127 SNR=00063 AFC=00047 EXN=00001 R22=0C00 R23=64FF ID=18434 T=001 P=02109
```

| 字段 | 含义 |
|---|---|
| `R19/R22/R23` | BK4802 寄存器回读（增益/静噪配置） |
| `RSSI/SNR/EXN` | 信号强度/信噪比/带外噪声 |
| `AFC` | 剩余频偏 |
| `ID` | 芯片 ID（0x4802=18434） |
| `T` | 最近判出的音调（0=无、1=mark、2=space） |
| `P` | 最近一次 ADC 原始值（旧版是捕获周期，已改） |

---

## 6. 主机验证工具

```powershell
# AX.25/APRS 参考实现自测
python tools/ax25_reference.py

# BK4802 频率字
python tools/bk4802_freq.py

# 生成标准 1200 baud APRS 测试音频（LSB-first）
python tools/gen_afsk_wav.py tools/test_aprs_144.wav
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

## 8. 待办 / 下一步

- [ ] 焊接 ST7567，启用 LCD 显示呼号/正文/S 表。
- [ ] 静噪最终标定：无信号静音、有信号正常出声（当前为解码测试临时关闭）。
- [ ] 考虑给 modem 加位时钟 PLL / 软判决，进一步抗声学链路抖动。
- [ ] 消息存储、告警（蜂鸣/振动）、按键 UI（Phase 5）。
- [ ] 整理端口文档：`firmware-stm32porject/PORT.md` 里的引脚说明仍偏旧，以本 README 为准。

---

## 9. 许可与合规

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
4. `BK4802P.pdf` 是 Beken 标注 *Confidential / NDA* 的数据手册，放在公开仓库有版权/NDA 风险，建议从公开仓库移除（本地保留）。
5. 本项目只做接收（RX-only）。
## 10. 参考项目

- [MM-Radio](https://github.com/doublehan07/MM-Radio)
- [BG7QKU STM32_SIMPLE_CONTROL_BK4802N](https://github.com/BG7QKU)
- [BG5ESN FMO BK4802 V2.00](https://github.com/BG5ESN/FMO-Radio-Module-BK4802-V2.00)
- [VP-Digi](https://github.com/sq8vps/vp-digi)
