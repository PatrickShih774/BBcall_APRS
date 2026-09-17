# BBcall_APRS 计划（BK4802P + STM32F103C8T6 + ST7567，APRS 寻呼机）

> 项目名（暂定）：**BBcall_APRS** —— 用 APRS（AX.25 / 1200 baud Bell202 AFSK）技术路线复刻 BB 机（无线寻呼机）。
>
> 参考项目：MM-Radio (BSD-2)、BG7QKU、BG5ESN FMO、VP-Digi。许可与合规见 README 第 11 节与 `../licenses/THIRD_PARTY_NOTICES.md`。
>
> 本文件以 **2026-09-11** 的实际实现为准；历史 bring-up 过程见 README 第 4 节「调试过程记录」。

## 1. 当前状态

- MCU：STM32F103C8T6（64KB Flash / 20KB SRAM）；
- 射频：BK4802P，21.25MHz 晶振，仅接收（RX-only），默认 **144.640MHz**；
- 显示：ST7567 12864 **已焊接并点亮**（2026-09-16），三态界面（待机 / 有未读 / 收件箱）在实机可用；
- 音频输入：BK4802 EAROP → 2.2k+22nF RC → 1µF 耦合 → 10k/10k 偏置 → PA1（ADC1_IN1，TIM3 9600Hz）；
- 按键：UP/DOWN/OK = PB12/PB13/PB14（上拉输入、按下为低），驱动与状态机已完成（详见 README 4.11）；
- 解码：**已完成**，实机可解真实 APRS 包（含 Mic-E / 普通位置 / 消息）；v0.5 实机连续收包正常；
- 最新固件：**v0.5**（2026-09-16 发布，`text=57324 / data=132 / bss=20220`）；此前 v0.4 于同日发布；
- 实测：SunSDR2 DX 手动 MOX 低功率可稳定解出；但**连续发射仍会漏包**，做不到"发一条看到一条"；
- 硬件前端目前是 **天线直接接 BK4802 ANT 脚、无滤波/匹配**，是漏包的主要瓶颈，改进见 README 第 9 节与本文第 10 节；
- RSSI/SNR 取解码当刻直读的 BK4802 寄存器 24 **原始码值（非标定 dBm）**，单次 I2C 读会随机失败，已用采样兜底。

## 2. 硬件与引脚（以实机为准）

| 功能 | 引脚 | 说明 |
|---|---|---|
| BK4802 SCL | PA9 | GPIO 位敲 I2C |
| BK4802 SDA | PA10 | 写推挽 / 读切输入 |
| BK4802 CE | PA0 | 输出高使能 |
| BK4802 DIO1 | PA8 | 输出低 |
| 对讲机侧 PWR/PTT | PA2 / PA4 | 与 MM-Radio 一致 |
| 音频输入 | **PA1 = ADC1_IN1** | EAROP → 隔直/RC/偏置 → ADC 解调 |
| LCD CS/CLK/MOSI/A0/RST | PB6 / PB3 / PB5 / PB4 / PB7 | 位敲 SPI |
| LCD 背光 | PB0 | 高电平点亮 |
| 蜂鸣 / 振动 | PA6 / PA7 | 输出 |
| LED | PB15 | 状态/告警 |
| 按键 上/下/确定 | PB12 / PB13 / PB14 | 上拉输入 |
| 调试串口 USART3 | PB10=TX / PB11=RX | 115200 8N1 |

> 注意历史文档（2026-09-08 版本）里的「FM 解调输出功放前级」不存在：BK4802P 没有引出功放前模拟音频；AUDC 是麦克风放大器共模节点。实际只用 **EAROP/EARON（D 类功放输出）** 取样，RC 低通 + 隔直 + 偏置后进 PA1。

## 3. 软件架构

```text
BK4802P FM 接收 → EAROP 音频（D 类 PWM）
  → 104 隔直 + RC 低通 + 10k/10k 偏置 → PA1(ADC1_IN1)
  → TIM3 9600Hz 采样（1200 baud × 8）
  → 最近 8 点 1200/2200Hz 定点相关（mark/space）
  → 16 相位并行 + 9 条多周期跳变对齐位时钟
  → NRZI → HDLC（LSB-first、去填充、CRC-16/X.25）
  → AX.25 → APRS（消息 / 位置 / Mic-E）
  → FIFO → 串口 / LCD
```

已实现的关键点：

- 1-bit / 2-bit CRC 纠错（`FIX`/`FIX2`）；
- 参考帧辅助重复包恢复（`REP`）；
- 解码帧 FIFO、重复包抑制（60s）、`RX/U/DUP` 统计；
- 自适应中频增益（`G=4/5/6`）、解码事件时间戳 `[T=...ms]`；
- IWDG 2 秒看门狗、I2C ACK/重试/总线恢复、栈与 RAM 审计修复。

## 4. 已发布版本

| 版本 | 内容 |
|---|---|
| v0.1 | 首版可解码固件（AX.25/APRS 基础链路） |
| v0.2 | 门限 20000→500、16 相位、跳变位时钟，成功率提升 |
| v0.3 | Mic-E/位置解析、1/2-bit 纠错、REP、FIFO、统计、自适应增益、时间戳、看门狗、I2C 健壮性、审计修复 |
| v0.4 | **LCD 实机点亮**（关 JTAG 释放 PB3/PB4、ST7567 电源序列、SEG/列偏移、180° 安装），三态界面接入固件，锁屏默认墙钟，收包立即切页 + 唤醒背光，呼号带 SSID，帧级 RSSI/SNR，20KB RAM 裁剪 |
| v0.5 | 修按键（收件箱为空时 ● 短按无反应），RSSI/SNR 改为**解码当刻直读寄存器 24**（采样只做兜底），S-meter 采样周期配置化，模拟器自检增强 |

## 5. BB 机功能规划（v0.4 → v1.0）

### 5.1 经典 BB 机核心功能

| 功能 | 说明 | 依赖 |
|---|---|---|
| 按地址接收 | 只显示发给本机的 addressee（呼号/SSID/别名） | 已有解析 |
| 消息提示 | 蜂鸣、振动、LED；响铃/静音/仅振动 | PA6/PA7/PB15 |
| 消息显示 | 呼号、时间、正文；长消息滚动 | ST7567 |
| 收件箱 | 列表、未读/已读、时间排序 | 外部存储 |
| 消息详情 | 翻页、返回、删除、全部删除 | 按键 + UI |
| 未读计数 | 主界面显示未读数 | 存储 |
| 消息时间 | 每条消息接收时间/日期 | RTC |
| 消息去重 | 同一 `msgid`/重复包只存一条 | 已有基础 |
| 存储容量 | 满提示、循环覆盖策略 | 外部 Flash/EEPROM |
| 重复提醒 | 未读消息定时再提醒 | 固件 |
| 按键锁/静音 | 防误操作、夜间静音 | 固件 |
| 背光/对比度 | 背光超时、对比度设置 | ST7567 |
| 状态显示 | 电量、RSSI/SNR/S 表、失联提示 | ADC + RSSI |
| 菜单/设置 | 提示方式、时间、背光、存储管理 | 固件 |

### 5.2 APRS 特有功能

| 功能 | 说明 | 依赖 |
|---|---|---|
| APRS 消息 | `:呼号:正文{msgid`，按呼号过滤 | 已有解析 |
| Ack/Rej | 显示"需要确认"；双向需加发射 | 仅接收时只显示 |
| 群发/公告 | 组地址、APRS Bulletin（`BLN...`） | 固件过滤 |
| 位置包 | `!`/`=`/`/`/`@`、Mic-E | 已有解析 |
| 方位/距离 | 显示经纬度、距离、方位 | LCD + 计算 |
| 天气/遥测/状态 | `_` / `T#` / `>` 包解析 | 扩展解析 |
| 中继路径 | 直发/中继、digipeater 列表 | 已有 |
| 台站列表 | 记录听到的呼号、时间、RSSI/SNR | 存储 |
| Object/Item | 对象/物品名称与位置 | 扩展解析 |
| 过滤 | 指定呼号/关键词/群组，屏蔽广告 | 固件 |
| 优先级 | 紧急/优先消息特殊提示 | 固件 |

### 5.3 状态与系统功能

- 工作状态：接收中、静音、未读、存储满、失联；
- 信号质量：RSSI/SNR/EXN、S 表、AFC；
- 电源：待机降功耗、低电告警、自动关机；
- 看门狗/异常复位记录（IWDG 已加）；
- 时间：RTC（DS3231 或内部 RTC）+ 备份电池；
- 配置保存：频率、呼号、提示方式、存储策略。

### 5.4 需要的额外硬件

- 外部存储：W25Q64（消息/台站/字库）或 AT24C512（设置）；
- RTC：DS3231 或 STM32 内部 RTC + 纽扣电池；
- 发射（可选 v1.0）：BK4802 发射通路 + 天线切换；需执照；
- 蓝牙/USB 配置：手机/PC 配置呼号、过滤、导入导出。

## 6. 版本路线与验收

> **版本号说明**：下表的 v0.4 / v0.5 是早期按功能划分的设想，实际发布内容与此不同
> （见第 4 节与 README 4.11）。后续按"先收得到、再存得住、再扩协议"推进，功能项保留、版本号不再强绑定。
> 当前真正要解决的问题是**漏包率**，方案见第 10 节。

### v0.4（MVP：能当 BB 机用）

- ST7567 显示呼号/时间/正文/未读数；
- 蜂鸣/振动提示与静音模式；
- 收件箱/消息详情/删除；
- 消息去重、未读计数；
- RSSI/SNR/电池状态页。
- **验收**：收到真实 APRS 消息后，LCD 显示正确、蜂鸣/振动触发、可翻页删除。

### v0.5（存储与菜单）

- W25Q64/AT24C512 消息与设置存储；
- RTC 时间戳；
- 菜单/设置（提示方式、背光、时间、存储管理）；
- 台站列表、信号质量页；
- 按键锁、重复提醒。
- **验收**：断电后消息与设置保留，菜单可配置，容量满有提示。

### v0.6（APRS 扩展）

- 群发/公告、Ack 显示、关键词过滤；
- 天气/遥测/状态包解析；
- 位置/方位显示、简化地图/相对位置；
- 台站列表排序与筛选。
- **验收**：能区分并显示消息/位置/天气/遥测/公告，过滤规则生效。

### v1.0（可选双向）

- BK4802 发射通路与 PTT；
- 回 Ack/Rej、发送 APRS 消息、双向寻呼；
- 需要业余无线电执照与硬件改造。
- **验收**：能完成一次"收消息→回 Ack"闭环。

### v0.7（界面文案汉化，延后执行）

> **本轮不执行**。字库链路已在 v0.4 阶段就绪（见 8.5），但界面文案替换刻意留到界面结构稳定之后再动，
> 否则每改一次版面就要按汉字行宽重排一次。

- 先把字库子集从 107 字扩到约 500 字（覆盖常用人名/地名），或接外置 SPI Flash 放全 GB2312；
- 逐个界面替换文案，按 [docs/design.md](design.md) §11 的文案规则（唯一权威规范）；
  旧「每行 8 汉字、内容区 3 行、ASCII 画在 y+5」等参数是按 8×16 字模定的，
  执行时须按 Fusion Pixel 12px 实际行宽重定（design.md §8）；
- 扩展 `tools/cn_chars.txt` 后必须重新生成并**同步布局常量**（坑见 8.5）；
- 不做拼音输入法（本项目没有键盘）。
- **验收**：主菜单 / 收件箱 / 阅读 / 状态页全中文显示，无缺字，且与现有 ASCII 屏的版面规范一致。

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| RAM/Flash 紧张 | 外置 W25Q64/AT24C512；字库子集化；`AX25_MAX_FRAME` 已降到 256 |
| 中文显示 | 字库链路**已落地**；UI v2.0 起三态界面改用 **Fusion Pixel 12px/10px**（design.md §3.5，历史方案见 8.5）；全量 GB2312 需外置 SPI Flash |
| 弱信号解码率 | 射频前端（BPF/匹配/LNA）、音频整形、重复包合并；见 README 第 9 节 |
| VOX/PTT 时序 | 用 150ms VOX 测试音频或手动 MOX；发射端关闭 ALC/压缩 |
| 静噪影响解码 | 解码时保持音频通路常开；软件静噪默认关闭 |
| 双向发射合规 | v1.0 前评估执照与发射滤波/天线切换 |

## 8. PC 端 LCD 模拟器（SDL2）

目标：在 PC 上用 SDL2 模拟 ST7567 128×64 单色点阵，**直接编译固件里的 `lcd_st7567.c` 绘图代码**，无需烧录 STM32 即可看到屏幕效果；并可注入测试 APRS 帧验证 UI。

### 8.1 架构

```text
固件代码（复用）: lcd_st7567.c / font8x16.h / ax25.c / aprs.c / modem.c
        │ LCD_SIM 分支
        ▼
PC 后端: simulator/src/lcd_sim.c（SDL2 + ST7567 命令状态机）
        │
        ▼
UI harness: firmware-stm32porject/Core/Src/ui_harness.c（三态：待机/有未读/收件箱；模拟器与真机单源共用）
        ▲
        │ ui_feed_ax25()
数据源: simulator/src/sim_feed.c（--wav 解调 / --replay 日志回放 / --demo 示例）
        │
        ▼
主程序: simulator/src/main.c（SDL 事件循环、按键、命令行、自检截图）
```

- `lcd_st7567.c` 加 `#ifdef LCD_SIM`：命令/数据走 `lcd_sim_*`，绘图/fb/字体完全复用；真机仍走 HAL GPIO。
- `sim_hal.c`：最小 HAL/GPIO/延时桩，让 `lcd_st7567.c` 可在 PC 编译。
- `lcd_sim.c`：实现 ST7567 命令子集（页/列地址、显示开关、反显、全亮、起始行、SEG/COM 方向），SDL2 渲染 128×64，支持放大、反显、背光、截图；按 `F3` 可对比两种面板 SEG 方向。
- `ui_harness.c`：收件箱数据模型（消息/位置/Mic-E/其它）+ 待机/列表/详情/删除界面，支持滚动与分页；解析复用 `ax25.c`、`aprs.c`。
- `sim_feed.c`：三种数据源 —— `--wav`（音频→`modem.c`→UI）、`--replay`（串口日志 `[RAW] hex=`）、`--demo`（内置示例）。
- 构建：`simulator/build_win.ps1`（Windows 免安装，TinyCC + 内置 SDL2，推荐）；`simulator/CMakeLists.txt` 与 `simulator/Makefile` 保留给装好 MSYS2 / vcpkg / w64devkit 的机器。

### 8.2 阶段与验收

| 阶段 | 内容 | 验收 |
|---|---|---|
| S1 | SDL2 骨架 + LCD 后端 + 测试画面 | 窗口显示 128×64；棋盘格/文字/反显/背光/截图正常；真机固件回归编译通过 |
| S2 ✅ | UI harness + 按键 + 测试帧注入 | 收件箱/详情/删除可用；能显示 `test_aprs_144.wav` 的 APRS 消息 |
| S3 ✅ | PC 端 modem 仿真（WAV → 解调 → HDLC → UI） | 不烧单片机即可跑通"音频→解码→LCD"全链路 |

### 8.3 当前状态

**S1 / S2 / S3 均已完成并在本机跑通**（2026-09-11）。

- 代码：`simulator/`（`main.c` / `lcd_sim.c` / `ui_harness.c` / `sim_feed.c` / `sim_hal.c`），
  内置 SDL2 2.32.10 于 `third_party/sdl2/`；
- 一键构建运行（无需 MSVC / MinGW / CMake）：

  ```powershell
  powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Run
  ```

- 复用的固件代码：`lcd_st7567.c`（驱动/绘图/字体）、`ax25.c`、`aprs.c`、`modem.c`（解调核心无 HAL 依赖）；
- 三种数据源：`--demo`（内置示例）、`--replay`（串口 `[RAW] hex=` 日志）、
  `--wav`（48kHz PCM → 5 点抽取到 9600Hz → `modem_adc_sample()` → `modem_get_frame()`）；
- 验收证据：
  - `--wav tools/test_aprs_144.wav` 解出 1 帧 `:BG5BLH   :Hello APRS 144.640`，
    详情页与期望绘制**逐像素差异 0**；
  - `--replay tools/sample_aprs_log.txt`（真实 5km 接收，13 帧）全部入箱，
    呼号/类型与 `tools/ax25_reference.py` 独立解码完全一致；
  - 16 路相位走廊各解一遍同一帧 → 重复抑制 15，与固件 `bbcall_app.c` 行为一致；
  - 真机固件回归编译通过；模拟器构建 0 错误 0 警告；
- **已修复屏幕水平翻转**：固件 `lcd_init()` 原为 `0xA1`（SEG 反向）+ `0xC0`（COM 正常），
  这个混搭会让整屏左右镜像；现改为 `0xA0` + `0xC0`。模拟器按状态机忠实复现，
  改后各界面文字立即正常（待机/收件箱/详情均已读屏验证）。

- **UI 重设计：复古寻呼机（BB 机）风格**。唯一权威规范是**[docs/design.md](design.md) v2.0**
  （§4 设计原则 / §5 版面骨架 / §6 三态屏幕 / §8 硬规则 / §11 文案 / §12 留白与移植 / §13 验证；
  原 UISkill.md 已并入，文件本体已删除，历史版本见 git）。
  摘要：`Dials = 6 / 2 / 7`（密度 7 -> 1px 细线分隔、不用卡片盒）；三态 = 待机（大格恒反显时钟 +
  本机/电量/未读三小格）/ 有未读（大格最新未读摘要 + APRS/RSSI/SNR）/ 收件箱（顶栏反显 +
  正文两行 + 元信息两行）；字体 Fusion Pixel 12px/10px（`tools/gen_fusion_font.py` →
  `firmware-stm32porject/Core/Inc/fusion_font.h`，374 字形）。
  诚实留白：电池位无采样显示 `--`、无 RTC 时日期行显示开机时长、未标定的 RSSI/SNR 不注入。
  更早的 boot/home/menu/detail/radio/about 七屏 chrome 系统与磁贴方案（G/G2c）仅作历史记录保留
  （原则并入 design.md §4/§5，版面废弃）。
- **Messenger 界面族**（版面参考 [GOGUFW-UV-K1-Messenger](https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger)，
  Apache-2.0，同样是 128x64 单色 LCD）。最初照搬它的 4 项启动器，实测**过于复杂**，已砍成两屏：
  - `messages` 收件箱：6 行，最新在上，`*` 未读 + 正文预览 + `NOW`/`12m`/`3h` 年龄；
  - `msgread` 阅读：`FROM:` + 年龄 + 正文 4 行 + `BACK`/`DEL`；
  - **砍掉** `COMPOSE`/`DRAFTS`（仅接收，只能存草稿＝假功能）、`SENT`（永远为空）、
    以及 Messenger 启动器本身（它的 HEARD 与主菜单 Heard 重复，去掉后少一层导航）。
  ``ackNNN`` 分流逻辑已内联进 `ui_feed_ax25()`（ACK 不污染收件箱）；msg_store.c 已删除。
    取舍与偏离见 [docs/design.md](design.md) §11。
### 8.4 后续（S4 候选）

已完成（原 S4 清单的一部分，已并入第 8.3 节）：删除二次确认、未读标记、详情分页、中继路径显示。

剩余候选：

- 时间戳老化显示（`AGE`：把 `12s` 换成 `3m` / `2h`，并给超过 N 小时的条目降级显示）；
- 收件箱持久化到文件，模拟器退出/重启后保留；真机对应外部 Flash / EEPROM；
- 把 `sim_feed.c` 的 WAV 解码包一层命令行批处理，做「批量音频回归测试」
  （喂一批 WAV，检查解出的呼号/正文是否与期望一致）；
- ~~按 [docs/design.md](design.md) §12.3 把 UI 状态机移植进固件~~
  **已完成（2026-09-16）**：三态 UI 移入 `Core/Src/ui_harness.c`（模拟器/真机单源共用），
  `bbcall_app.c` 接线完成；移植记录与已知补全见 [docs/design.md](design.md) §12.4，
  待 LCD 焊上后真机烧录验证。
## 8.5 中文显示方案（参考 Dondji）

> **已被取代（2026-09-16）**：UI v2.0 三态界面改用 **Fusion Pixel 12px/10px 字模**
> （`tools/gen_fusion_font.py` 从原型内嵌字表生成 `firmware-stm32porject/Core/Inc/fusion_font.h`，374 字形），
> 规范见 [docs/design.md](design.md) §3.5。下面的 GNU Unifont 16x16 子集 / `CN_FONT_ENABLED` 链路
> 保留作历史记录，新 UI 不再编译 `cn_font.c`，`build_win.ps1` 也不再自动检测 `cn_font_data.h`。

**现状**：ASCII 字模（`font8x16.h` / `font6x8.h`）已就绪；中文字库链路已打通（见下），
**界面文案汉化延后到 v0.7**。

**参考**：[EthanYan6/Dondji](https://github.com/EthanYan6/Dondji)（Apache-2.0，101★，泉盛 UV-K1/UV-K5 V3）
是目前中文做得最完整的同类固件：菜单汉化 + 中文输入法 + 中文信道名。它的字库方案值得照搬：

| 项 | Dondji |
|---|---|
| 字模 | 12x12，每字 12 行 x uint16_t = 24 字节 |
| 存放 | **外部 SPI Flash**（基址 0x024000），固件只留 `CN_FONT_*` 布局常量 |
| 布局 | `[位图][Unicode 索引 4B/项 升序][拼音表][版本字节]`，6766 字共 205,367 B |
| 字源 | WenQuanYi Bitmap Song 9pt |

**我们采用**：同一套**布局形状**（位图 + 4 字节 Unicode 升序索引 + 版本字节），
将来接外部 SPI Flash、再加拼音表时，读取逻辑一行不用改。

**我们不采用**：

- **字源不能用 WQY Bitmap Song**：GPL v2（仅此一版）+ 字体嵌入例外，与本项目 GPL-3.0
  **不兼容**（GPLv2-only 无法并入 GPLv3）。改用 **GNU Unifont**
  （2013 起 GPLv2+ 或 OFL-1.1 双许可，且本身就是 16x16 点阵）；见 `../licenses/THIRD_PARTY_NOTICES.md`。
- **暂不做拼音输入法**：本项目没有键盘，信道名也暂不支持中文输入。

**片上预算**（STM32F103C8T6，64KB Flash；当前 text 24,160 B，可用约 38KB）：
每字 `16x16 位图 32B + 索引 4B = 36B`。

| 字数 | 占用 | 说明 |
|---|---|---|
| 107 | 3.9 KB | 当前子集（菜单/状态用词 + 常用字），**实测编译后 4,028 B** |
| 500 | 18 KB | 可覆盖常见人名地名 |
| 约 1055 | 38 KB | 片上极限，不留余量 |

全 GB2312（6763 字）需约 243KB，**必须外置 SPI Flash**（第 7 节风险对策里的 W25Q64）。

**工具链**

```bash
python tools/gen_cn_font.py --unifont <unifont.hex> --chars-file tools/cn_chars.txt \
    --out-header firmware-stm32porject/Core/Inc/cn_font_data.h --out-bin tools/cn_font.bin
```

`CN_FONT_ENABLED=1` 时启用（`bbcall_cfg.h`，默认 0）；模拟器 `build_win.ps1` 检测到
`cn_font_data.h` 会自动打开。字符清单在 `tools/cn_chars.txt`，`tools/cn_font.bin` 是生成的裸字库
（`.gitignore` 已排除，可随时重新生成）。

> 真机启用时注意：`Core/Src/cn_font.c` 需要**在 STM32CubeIDE 里刷新工程**才会进入构建
> （命令行 `make` 用的是已有 makefile，不会自动收录新文件）。

**踩坑提醒（来自 Dondji 文档）**：重新生成字库后**必须同步固件的布局常量**。
它那边的现象是只刷了新字库 bin 却忘了改 `CN_FONT_PY_OFFSET`，固件按旧偏移去扫拼音区，
结果是「任意拼音候选错乱、大量音节匹配失败」，不是个别字的问题而是整表错位。
我们同理：位图长度一变，索引区起始地址就变。

**模拟器自检**：`--screen cnfont` 逐页显示字库全部字形；About 页显示 `CN FONT <字数>`
（未启用时显示 `CN FONT OFF`），便于真机核对刷入的字库版本。

**阶段边界**：本节只覆盖「字库」本身。**界面文案的汉化不在本轮范围**，排期见第 6 节 v0.7。
## 9. 参考项目与许可

- MM-Radio（BSD-2-Clause，主参考/工程底座）；
- BG5ESN FMO（MIT，频率字参考）；
- VP-Digi（GPL-3.0，AFSK/AX.25 参考）；
- BG7QKU 仓库未声明 License，仅作资料参考，不复制代码。
- 本项目 GPL-3.0；详见 README 第 11 节、`../licenses/THIRD_PARTY_NOTICES.md` 与 `../licenses/`。

## 10. 下一步方案：把漏包率降下来（v0.5 之后）

背景：v0.5 实机解码正常，但**连续发射仍会漏包**。瓶颈优先级判断为：
射频前端（天线直连 BK4802、无匹配无滤波）> 音频电平 / IF 增益 > 解调判决。
三步按"先量化、再动硬件、最后动算法"排，每步都有可对比的验收口径。

### 10.1 P1 量化漏包（软件，先做，约 1 天）

**目的**：把"有信号但没解出"与"根本没信号"分开。否则任何改动都无法评估效果。

- **底噪跟踪**：RSSI（寄存器 24，100ms 采样）持续记录，取长时间窗口的低分位作为 `noise_floor`
  （例如 5s 窗口的最小值，再对 1 分钟取低 10% 分位）；
- **疑似漏包判定**：RSSI 快速上升超过 `noise_floor + N`（N 先取 20 码值）时开一个 1 秒窗口；
  窗口内若没有任何 CRC 正确的帧入队，记一次疑似漏包，同时保存窗口内 RSSI/SNR 峰值与 MK/SP/OT 差值；
- **新增计数**：`s_rf_pkt_cnt`（触发窗口次数）、`s_rf_miss_cnt`（其中没解出的）、`s_rf_noise`（底噪）、
  `s_rf_miss_last`（最近一次漏包时的峰值 RSSI/SNR）；
- **观测**：ST-Link 的 Live Expressions（无需串口）；可选加一屏隐藏诊断页（待机 / 有未读态长按 ● 切换，
  显示底噪、最近窗口的 MK/SP/OT、FIX/REP、RSSI 读写计数、rx_count）；
- **验收**：同一发射条件连发 20 包，输出形如"解出 x / 疑似漏包 y / 无信号 z"，率值可复现、可对比。

### 10.2 P2 射频前端（硬件，收益最大）

- 天线到 BK4802 ANT 之间加 π 型或 L 型匹配（串 L + 两端并 C），必要时加 144MHz 带通 / SAW；
- 无 VNA 时的调法：弱信号源 + 观察 RSSI/SNR 最大、EXN 最小（README 第 9.1 节）；
- **验收**：同样发射条件下 P1 的漏包率明显下降；弱信号 RSSI 提升 6dB 以上；
  强信号不再把前端压死（配衰减器验证）。

### 10.3 P3 软件解码增强（不动硬件）

- **P3a 多相位软判决合并**：现在 16 相位 + 9 条跳变解码器各自硬判决、谁先 CRC 通过就算成功；
  改为把各路似然（m1-m2）按位对齐后合并再做判决，弱信号下可多救回一部分帧；
- **P3b 纠错窗口与阈值调优**：`AX25_SYN_MAX_BITS`（160 字节同步窗）、`REF_MAX_DIFF=4`、
  1/2-bit 纠错搜索范围，都能用离线素材回放调参，不必上真机；
- **P3c 自适应判决门限**：目前 v = m1-m2 的零点固定；改为按 MK/SP 分布动态调偏置，抗频偏与增益漂移；
- **验收**：先用离线素材（`tools/test_bg5blb12*.wav` + `--wav`、真实日志 `--replay`）对比"解出帧数"；
  再上真机按 10.1 的口径对比漏包率。

### 10.4 现有观测与回归工具（直接用，不必新建）

- 离线：模拟器 `--wav`（与真机同一份 modem.c）、`--replay`、`--rf R,S`、`tools/verify_ui.py` 逐像素校验；
- 真机无串口：ST-Link Live Expressions 读
  `modem.c::s_mark_hits / s_space_hits / s_other_hits / s_fix_count / s_fix2_count / s_rep_count`、
  `bbcall_app.c::rx_count / s_rf_ok_cnt / s_rf_fail_cnt / s_rf_last_raw`、`ui_harness.c::s_box[0]`；
- 有串口时：`[FRAME] ... RSSI=073 SNR=019 (decode-now)` 与每 2s 的 `R19=...` 行；
- 注意 SWO 不可用：SWO 是 PB3，已被 LCD 当 SCLK 占用。

### 10.5 本轮明确不做（避免扩散）

- 发射 / 中继（digipeater）、压缩位置、对象 / 状态 / 遥测 / 第三方包解析（属第 6 节 v0.6 计划）；
- 存储与 RTC（第 6 节"存储与菜单"档）：先把"收得到"做扎实，再谈"存得住"；
- 界面文案汉化（第 6 节 v0.7，已延后）。
