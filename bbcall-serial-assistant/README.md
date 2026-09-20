# BBcall 串口调试助手

面向 **BBcall_APRS** 固件的 PC 端串口调试与配置助手。整个工具是**一个 HTML 文件**，双击即用：通过浏览器 **Web Serial API** 直连设备的 USART3，实时镜像 128×64 点阵屏、调参、对时、导出设备日志。没有硬件时可用**内置模拟器**完整走一遍全部流程。

| 项目 | 值 |
|---|---|
| 交付文件 | [`docs/bbcall-serial-assistant.html`](docs/bbcall-serial-assistant.html)（单文件，无依赖、无构建） |
| 目标设备 | BBcall_APRS：BK4802P + STM32F103C8T6 + ST7567 128×64（仅接收，默认 144.640 MHz） |
| 串口参数 | USART3 / **115200 8N1** / 文本行协议（`\r\n` 结尾） |
| 接线 | PB10 → 适配器 RX，PB11 ← 适配器 TX，共地 |
| 运行环境 | Chrome / Edge 89+ 桌面版 |

---

## 快速开始

1. **关闭 SSCOM**（串口为独占资源，助手连接前必须释放）。
2. 用浏览器打开 `docs/bbcall-serial-assistant.html`。
3. 顶栏选择串口与波特率（默认 115200），点击 **连接设备**。
4. 无硬件时点击 **模拟设备**，界面会用内置状态机伪造固件回包，所有交互可照常演示。
5. 在控制台输入 `STAT?` 回车，或点快捷命令 `STAT?`，左侧状态栏与 128×64 屏幕镜像即开始刷新。

> Web Serial 只在安全上下文（`https://` 或 `localhost`）可用。直接双击打开的 `file://` 页面在多数浏览器中拿不到串口权限，此时请用模拟设备，或用任一本地静态服务器（`npx serve` / VS Code Live Server）打开。

---

## 界面结构

三栏固定工作区，全部命令与字段严格对齐固件实际输出：

| 区域 | 内容 |
|---|---|
| **顶栏** | 串口 / 波特率 / 连接与断开 / 模拟设备开关 / 链路状态灯 |
| **左栏 · 设备** | RSSI / SNR / 频点 KPI，常看状态表，可折叠「更多诊断计数」，128×64 屏幕镜像 |
| **中栏 · 控制台** | 分级过滤（全部 / 帧 / 状态 / 记录 / 错误）、检索、跟随滚动、日志导出、命令输入与历史、快捷命令 |
| **右栏 · 配置** | 标签页：**调参** / **对时** / **工具** |
| **状态栏** | 已收 / 帧 / 满箱丢弃 / RTC |

---

## 功能一览

**设备状态**
- `STAT?` 全字段解析：`up/RSSI/SNR/G/AGC/SQ/SQN/FREQ/DUPMS/I2CE/ID/RX/U/DUP/UNREAD/INBOX/DROPN/FIX/FIX2/REP/ISRavg/ISRmax/T`
- S-meter：优先采用设备心跳 `S=` 回包里的 `bk4802_get_smeter()` 读数，静音状态由 `M=` 位反向确认
- 2 秒周期 `R19=` 与 500ms 周期 `S=` 心跳持续刷新，连接后无需反复手动查询

**屏幕镜像**
- Canvas 像素缓冲离屏绘制 128×64，再整数倍放大（关闭插值），复刻 ST7567 点阵
- LCD / OLED 双主题，以及待机 / 新消息 / 消息 三态切换，与设备 `docs/design.md` 的 1-bit 规范一致

**控制台**
- 5 个过滤分组内部聚合 `FRAME/RAW/STAT/CFG/RTC/REC/ERR` 标签，日志行保留原始标签配色
- 检索、跟随滚动、环形缓冲上限 1200 行
- 导出为 `.log`（时间戳 + 标签 + 原文，`\r\n` 换行）
- 命令历史（↑ / ↓，最多 60 条，`localStorage` 持久化）

**配置下发**（与固件一一对应）

| 控件 | 命令 |
|---|---|
| 接收频率 | `FREQ=<kHz>`（≥ 24000，含 144.640 / 144.390 / 145.825 预设） |
| 中频增益 | `GAIN=0..7`（3 dB/级，写入自动关 AGC） |
| 自动增益 | `AGC=0/1` |
| 静噪门限 | `SQ=0..255` |
| 静噪噪声门限 | `SQN=0..255` |
| 音频静音 | `MUTE=0/1` |
| 去重窗口 | `DUPMS=0..60000` |
| 恢复默认 | 一键下发固件默认值 |

**对时**：`TIME=<本地时间>`（一键同步）、`TIME?`、`TRIM=±ppm`、`TRIM?`
**工具**：`PING`、`INBOX?`（收件箱导出）、`LOG?`（设备端掉电不丢日志整段导出）、`LOG0`（清空重记）

---

## 技术方案

| 层 | 技术 | 说明 |
|---|---|---|
| 载体 | 纯 HTML / CSS / 原生 JS | 单文件，无框架、无打包，另存即用 |
| 串口链路 | **Web Serial API** | `requestPort` / `getPorts` 选口，`ReadableStream` 读、`WritableStream` 写，`TextDecoder` 流式解码、`TextEncoder` 写 `\r\n` |
| 降级通道 | 内置设备模拟器 | `setInterval` 状态机伪造固件回包，无硬件可演示全部交互 |
| 协议解析 | 行缓冲 + 前缀分派 | 按 `[STAT]` / `[R19]` / `[S=]` / `[FRAME]` / `[INBOX]` / `[LOG]` 分派到状态表、S-meter、日志流 |
| 屏幕镜像 | Canvas 像素缓冲 | 离屏 128×64 `getImageData` → 主画布整数倍放大，`imageSmoothingEnabled=false` |
| 持久化 | `localStorage` | `bbcall.` 前缀：`port` / `tab` / `follow` / `lcdTheme` / `lcdState` / `hist` |
| 导出 | `Blob` + `createObjectURL` | 日志与设备日志落盘 |

**为什么是单文件浏览器方案**：BBcall 是纯接收设备、调试命令集有限，单文件原型在「发给别人直接打开就能用」这一点上最优。

**需要换方案的情形**：
- 要无人值守长期挂机抓包 → 页面 + 本地桥（页面走 WebSocket，另一端接项目已有的 `tools/serial_bridge.ps1`，COM 口由 PowerShell 独占），绕开安全上下文与串口独占限制。
- 要免浏览器、开机自启、原生系统集成 → Electron / Tauri + `node-serialport`，或 Python + PySerial + PySide6。

---

## 目录

本次生成的设计文件与设计规范已全部收进 `docs/`，项目根目录只留本索引：

```
README.md                            ← 本文件：项目索引
docs/
├── bbcall-serial-assistant.html     ← 助手本体（单文件，双击即用）
├── design-reference.html            ← 设计规范：令牌 / 排版 / 点阵 / 栅格 / 组件 / 交互状态
├── user-guide.md                    ← 操作手册
└── serial-protocol.md               ← 串口协议参考
```

| 文件 | 内容 |
|---|---|
| [`docs/bbcall-serial-assistant.html`](docs/bbcall-serial-assistant.html) | 助手本体（单文件，无依赖、无构建） |
| [`docs/design-reference.html`](docs/design-reference.html) | 设计规范：色彩 / 排版 / 点阵 / 栅格令牌、组件库与交互状态、128×64 镜像规范（可交互） |
| [`docs/user-guide.md`](docs/user-guide.md) | 操作手册：连接、各面板、快捷键、故障排查 |
| [`docs/serial-protocol.md`](docs/serial-protocol.md) | 串口协议参考：命令表、周期与事件输出、字段说明 |

---

## 已知限制

- **安全上下文**：Web Serial 要求 `https` 或 `localhost`；`file://` 下退化为模拟设备。
- **串口独占**：连接前必须关闭 SSCOM 等占用同一串口的程序。
- **浏览器独占**：Web Serial 目前仅 Chrome / Edge 系桌面版支持。
- **单文件体积**：约 87 KB、约 1600 行，超出常规 1000 行软约束，为「另存即用」刻意保留，未拆分。
- **协议随固件演进**：所有解析规则对齐固件 `bbcall_app.c` / `pktlog.c` 当前实现，固件输出格式变更时需同步更新解析分支。

---

## 相关资源（只读参考）

- `C:\CodexProject\BBcall_APRS\firmware-stm32porject\Core\Src\bbcall_app.c` —— 命令与输出格式权威来源
- `C:\CodexProject\BBcall_APRS\firmware-stm32porject\Core\Src\pktlog.c` —— 设备端日志（`LOG?` / `LOG0`）格式
- `C:\CodexProject\BBcall_APRS\docs\design.md` —— 设备屏 UI 规范（色彩、骨架、三态）
- `C:\CodexProject\BBcall_APRS\tools\serial_bridge.ps1` —— 串口桥行为参考
