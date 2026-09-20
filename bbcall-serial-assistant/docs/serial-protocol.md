# 串口协议参考 — BBcall_APRS

本文件描述 BBcall_APRS 固件 USART3 控制台的行协议，供 PC 助手解析与调试对照。
权威实现：`firmware-stm32porject/Core/Src/bbcall_app.c`（命令）、`Core/Src/pktlog.c`（设备日志）。

---

## 1. 链路层

| 项 | 值 |
|---|---|
| 接口 | STM32 USART3 |
| 参数 | **115200 8N1** |
| 编码 | ASCII / UTF-8（中文 `info` 字段为 UTF-8 字节原样透传） |
| 行结束 | 设备输出 `\r\n`；PC 发送命令需以 `\r\n` 结尾 |
| 命令大小写 | **大小写敏感**：固件用 `strncmp` 逐字节比较，命令必须**全大写**（`stat?` 不会匹配；助手统一发送大写） |
| 命令前导空格 | 允许（固件跳过 `' '` 与 `'\t'`） |

所有输出行以**方括号标签**开头（周期计数器行 `S=` / `R19=` 除外），标签决定助手的日志分类：

| 标签 | 含义 | 助手分组 |
|---|---|---|
| `[CFG]` | 命令回执（含 `err:` 错误） | 状态 |
| `[STAT]` | `STAT?` 全状态打包 | 状态 |
| `[RTC]` | 时间与校时 | 状态 |
| `[FREQ]` | 调频诊断 | 状态 |
| `[LCD]` | 屏幕/UI 启动信息 | 状态 |
| `[I2C]` | I2C 总线错误 | 错误 |
| `[RAW]` | 原始帧十六进制（可选，`BBCALL_RAW_LOG`） | 帧 |
| `[FRAME]` | 解码成功的一帧 | 帧 |
| `[FIX]` / `[REP]` / `[FIX2]` | 帧的位修正 / 重复 / 二次修正标记 | 帧 |
| `[DUP]` | 去重抑制提示 | 帧 |
| `[INBOX]` | 收件箱导出 | 记录 |
| `[LOG]` | 设备端 Flash 日志 | 记录 |

---

## 2. 命令表

### 2.1 查询类

| 命令 | 成功回显 |
|---|---|
| `STAT?` | `[STAT] up=…ms RSSI=… SNR=… G=… AGC=… SQ=… SQN=… FREQ=… DUPMS=… I2CE=… ID=… RX=… U=… DUP=… UNREAD=… INBOX=… DROPN=… FIX=… FIX2=… REP=… ISRavg=…us ISRmax=…us T=…` |
| `PING` | `[CFG] pong` |
| `TIME?` | `[RTC] now YYYY-MM-DD HH:MM:SS Www src=<源>`；未对时 → `[RTC] no-time (send TIME=YYYY-MM-DD HH:MM:SS)` |
| `TRIM?` | `[RTC] src=<源> trim=<±ppm>ppm (eff=<±ppm>ppm) div=<分频>` |
| `DUPMS?` | `[CFG] DUPMS=<ms>ms` |
| `INBOX?` | 摘要行 `[INBOX] n=… unread=… dropn=…`，随后每项一行（见 §2.3） |
| `LOG?` | 摘要行 `[LOG] n=… cap=… seq_last=… lost=… order=old2new`，随后每条一行（见 §2.4） |

### 2.2 设置类

| 命令 | 取值 | 成功回显 | 错误回显 |
|---|---|---|---|
| `GAIN=<n>` | `0..7`，3 dB/级 | `[CFG] gain=<n> (<n*3>dB) AGC=0` | `[CFG] err: GAIN=0..7 (3dB/step)` |
| `AGC=<n>` | `0/1` | `[CFG] AGC=<n>` | `[CFG] err: AGC=0/1` |
| `SQ=<n>` | `0..255` | `[CFG] SQ=<n>` | `[CFG] err: SQ=0..255` |
| `SQN=<n>` | `0..255` | `[CFG] SQN=<n>` | `[CFG] err: SQN=0..255` |
| `FREQ=<kHz>` | `≥ 24000` | `[CFG] FREQ=<kHz>kHz` | `[CFG] err: FREQ=<kHz>` |
| `MUTE=<n>` | `0/1` | `[CFG] MUTE=<n>` | `[CFG] err: MUTE=0/1` |
| `DUPMS=<n>` | `0..60000` | `[CFG] DUPMS=<n>ms` | `[CFG] err: DUPMS=<ms> 0..60000` |
| `TIME=<时> ` | `YYYY-MM-DD HH:MM:SS` | `[RTC] set YYYY-MM-DD HH:MM:SS Www src=<源>` | `[RTC] err: bad TIME format` / `[RTC] err: no clock source` |
| `SETTIME=<时>` | 同上（`TIME=` 别名） | 同上 | 同上 |
| `TRIM=<±ppm>` | 有符号整数 | `[RTC] trim set trim=<±ppm>ppm (eff=<±ppm>ppm) div=<分频>` | `[RTC] err: use TRIM=+520 / TRIM=-120 (ppm)` |
| `LOG0` | — | `[LOG] erased n=<旧条数> -> 0` | — |

> `GAIN=` 写入会**关闭 AGC** 并锁定当前档位；`AGC=1` 后固件按 RSSI 带迟滞自动升降档。
> 数值一律以**原始十进制**打印，不做零填充。

### 2.3 `INBOX?` 输出

```
[INBOX] n=<总条数> unread=<未读> dropn=<满箱丢弃>
[INBOX] i=<序号>/<总数> t=<时间> src=<源呼号> dst=<目的> path=<路径> rssi=<值|--> snr=<值|--> f=<OK|FIX|REP> r=<0|1> h=<哈希hex> info=<正文>
```

### 2.4 `LOG?` 输出（设备端掉电不丢日志）

```
[LOG] n=<条数> cap=<容量> seq_last=<序号> lost=<丢失> order=old2new
[LOG] i=<序号>/<总数> t=<时间|UP+<秒>s> src=<呼号[-SSID]> rssi=<值|--> snr=<值|--> f=<OK|FIX|REP> h=<哈希hex> info=<正文>
```

- `t` 有两种：已对时 → `YYYY-MM-DD HH:MM:SS`；未对时 → `UP+<开机秒数>s`。
- `info` 中的控制字符（`< 0x20`、`0x7F`）被替换为 `.`，保证一行一条、可解析。

---

## 3. 周期输出（非请求）

设备持续主动上报，助手无需轮询：

### 3.1 `S=` 心跳（500 ms）

```
S=<s-meter> MK=<Δ> SP=<Δ> OT=<Δ> A0=<amin> A1=<amax> M=<0|1>
```

- `S=` 为 `bk4802_get_smeter()` 读数，助手 S-meter 条优先采用此值。
- `M=` 为音频静音位，可反向确认 `MUTE=` 是否生效。

### 3.2 `R19=` 诊断（2 s）

```
R19=<reg19> RSSI=<n> SNR=<n> G=<n> AGC=<n> RX=<n> U=<n> DUP=<n> FIX=<n> FIX2=<n> REP=<n> I2CE=<n> AFC=<n> EXN=<n> R22=<hex> R23=<hex> ID=<n> T=<n> P=<n>
```

I2C 读失败时，本周期改为先打印一行 `[I2C]`：

```
[I2C] scl=<n> sda=<n> relPA8=<n> ack=<n>
```

---

## 4. 事件输出

### 4.1 解码帧

```
[FRAME] src=<呼号[-SSID]> dest=<目的> path=<(none)|中继列表> ctrl=<n> info=<正文> RSSI=<n> SNR=<n> (decode-now|peak-fallback)
```

- `path` 多个中继以 `,` 分隔，格式 `<呼号>-<SSID>`，带 `*` 表示已中继过。
- 重复包被抑制时只打印 `[DUP] src=<呼号>`，**不**打印 `[FRAME]`，也不计 `RX`。
- 位修正 / 重复标记在行首以 `[FIX] ` / `[REP] ` 前缀出现。
- 原始十六进制（编译期 `BBCALL_RAW_LOG` 打开时）：

```
[RAW] len=<字节数> hex=<hex...>
```

### 4.2 启动横幅

```
[LCD] UI v2.0 three-state (design.md), keys: UP/DOWN/OK, long-press 620ms
[RTC] cfg=<1|0> src=<源> lse=<n>ms trim=<±ppm>ppm (eff=<±ppm>ppm) div=<分频> time=… wday=<0..6>
[LOG] init pages=<n> record=72B cap=<n>
```

未焊接屏幕时打印 `[LCD] disabled (not soldered yet), console mode`。
无 RTC 时钟源时 `[RTC] cfg=… src=none ERR: no RTC clock source (cfg=1 -> check 32.768k crystal / load caps)`。

### 4.3 调频诊断

```
[FREQ] want r2=<hex>
[FREQ] read r2=<hex>
[FREQ] read all FFFF -> re-init BK4802
```

---

## 5. 字段索引

| 字段 | 含义 |
|---|---|
| `up` | 开机时长（ms，`HAL_GetTick()`） |
| `RSSI` | 芯片原始 RSSI 读数（非 dBm） |
| `SNR` | 芯片原始 SNR 读数 |
| `G` | 中频增益档 `0..7`（×3 dB） |
| `AGC` | 自动增益开关 `0/1` |
| `SQ` | 静噪 RSSI 门限 `0..255` |
| `SQN` | 静噪噪声门限 `0..255` |
| `FREQ` | 接收频率（kHz） |
| `DUPMS` | 去重窗口（ms） |
| `I2CE` | I2C 错误累计 |
| `ID` | 芯片 ID 寄存器（27） |
| `RX` | 成功接收帧数 |
| `U` | 唯一信源数（≤16） |
| `DUP` | 去重抑制帧数 |
| `UNREAD` | 未读消息数 |
| `INBOX` | 收件箱条数 |
| `DROPN` | 满箱丢弃数 |
| `FIX` / `FIX2` / `REP` | 位修正 / 二次修正 / 重复帧计数 |
| `ISRavg` / `ISRmax` | 100 ms 采样中断耗时均值 / 峰值（µs） |
| `T` | 说明用途见所在行：`[STAT]` 中为 RTC 时间；`R19=` 中为音调状态 |
| `AFC` / `EXN` | 芯片寄存器 25 / 26 读数 |
| `M` | 音频静音位 |
| `r` | 收件箱该项是否已读（`1` = 已读） |
| `h` | 帧哈希（hex），用于去重与对照 |

---

## 6. PC 助手解析要点

助手实现于 `bbcall-serial-assistant.html` 的 `parseLine()` / `classify()`，关键约定：

1. **按前缀分派** —— 先判 `[FRAME]` / `[RAW]` / `[INBOX]` / `[LOG]` / `[STAT]` / `[CFG]` / `[RTC]` / `[I2C]`，再判无标签的 `S=` / `R19=`。
2. **`[FRAME]` 的截断** —— `info=` 之后紧接 ` RSSI=…`，解析正文时需在 ` RSSI=` 处截断，否则会把射频读数当作消息内容。
3. **多行帧** —— `[RAW]` 与 `[FRAME]` 前有独立空行；`[FRAME]` 后可能跟随以空格开头的 ` msg=` / ` [POS]` 续行。
4. **不做零填充假设** —— 固件打印原始十进制，解析用宽松正则，勿按固定宽度切字段。
5. **`[LCD]` / `[FREQ]` 不是错误** —— 它们是正常启动 / 调频诊断，归入「状态」分组；「错误」组只收 `[I2C]`、`err:`、`[LOG] append failed`。

固件输出格式变更时，同步更新上述分支；字段对照以本文 §5 为准。
