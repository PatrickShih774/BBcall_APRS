# BBcall_APRS PC LCD 模拟器（SDL2）

用 SDL2 模拟 ST7567 128×64 单色点阵，**直接编译固件里的代码**：

```text
lcd_st7567.c  ST7567 驱动 / 绘图原语（含反显填充）/ Fusion Pixel 字模绘制
ax25.c        HDLC 去填充 / CRC-16/X.25 / 地址解析 / 纠错
aprs.c        APRS 消息 / 位置 / Mic-E 解析
modem.c       1200 baud Bell202 AFSK 解调（16 相位 + 9 条跳变对齐）
ui_harness.c  三态界面（待机 / 有未读 / 收件箱）与按键状态机
```

不烧录 STM32 就能看到屏幕效果，也能回放真实接收数据。

## 快速开始（Windows 免安装，已在原机器验证）

仓库自带 TinyCC（`third_party/tcc/`）与 SDL2（`third_party/sdl2/`），**不需要** MSVC / MinGW / CMake：

```powershell
# 编译
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1

# 编译 + 无窗口自检（写出 BMP）
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Selftest

# 编译 + 打开窗口
powershell -ExecutionPolicy Bypass -File simulator\build_win.ps1 -Run
```

产物在 `simulator/build-win/bbcall_sim.exe`（`SDL2.dll` 会被一并拷到该目录）。

> 若提示 `could not write 'bbcall_sim.exe': Permission denied`，说明模拟器窗口还开着，
> Windows 会锁定正在运行的 exe；按 Esc 退出后再构建。

## 直接运行已编译的 exe

```powershell
cd simulator\build-win

# 内置示例数据
.\bbcall_sim.exe --scale 4

# 回放真实串口日志（仓库自带样例，13 个真实 APRS 帧）
.\bbcall_sim.exe --replay ..\..\tools\sample_aprs_log.txt --scale 4

# 直接解调 WAV（走固件 modem.c 的完整链路）
.\bbcall_sim.exe --wav ..\..\tools\test_aprs_144.wav --scale 4

# 无窗口自检：把某个屏幕态渲染成 BMP（--demo 注入内置示例帧）
.\bbcall_sim.exe --selftest --demo --screen unread --out unread.bmp
```

### 命令行参数

```text
--scale N        放大倍数 1..12（默认 4）
--selftest       无窗口（SDL dummy 驱动）渲染一帧并写出 BMP
--out FILE       自检输出文件名（默认 sim_selftest.bmp，按 scale 放大）
--screen NAME    idle | unread | inbox | pattern（默认按未读数自动选待机/有未读）
--wav FILE       WAV -> modem.c 解调 -> 收件箱
--rf R,S         回放时按真机那样在入箱前注入 RSSI,SNR（芯片原始读数，如 73,19）
--replay FILE    串口日志 [RAW] len=.. hex=.. 回放到收件箱
--demo           注入内置示例帧（无外部文件时的默认）
--clock SEC      设备时钟固定值（自检截图用，决定时钟/时刻显示）
--wallclock W,M,D  模拟 RTC：待机大格显示 周W M/D（如 3,9,16 = 周三 9/16）
--mycall CALL    本机呼号（待机右上格，默认 NOCALL）
--batt N         电量挡位 0 低 / 1 中 / 2 高（默认无采样，显示 --）
--keys LIST      按键序列（1=上 2=下 3=确定 4=长按确定），验证导航路径
--keymap         打印并自检键盘映射
-h / --help      帮助
```

屏幕坐标、字模、按键语义以仓库根目录 [design.md](../design.md) v2.0 为唯一权威规范
（§5 骨架 / §6 三态 / §8 硬规则 / §13 验证）。

### 按键

按键模型与真机一致，只有 **▲ ▼ ●** 三个键（物理键与数字键等价）：

| 物理键 | 数字键 | 内部键码 | 作用 |
|---|---|---|---|
| ↑ / ↓ | `1` / `2` | 1 / 2 | 收件箱内滚动（先滚正文，到底再翻条）；待机/有未读态为死键 |
| Enter | `3` | 3 | 有未读 → 打开收件箱；收件箱内标已读并前进到下一条 |
| Enter 长按 ≥620ms | `4` | 4 | 退出收件箱；未读清零自动回待机 |
| I / B | — | — | 反显 / 背光开关 |
| F3 / F12 / Esc | — | — | 面板 SEG 方向 / 截图 / 退出 |

> 窗口必须先点一下拿到焦点，否则按键不会送进来。
> 键映射本身可自检：`.\bbcall_sim.exe --keymap`（6 项，逐条打印 SDL 键 -> 内部键码，
> Enter 长按 620ms 的判定在事件循环里，不在这 6 项内）。
> 加这个自检是因为踩过一次坑：文档写"按 7 是待机页"，但键盘当时只绑了字母 S，数字键根本没映射。

### 导航

```text
无未读 → 待机态     大格恒反显：时钟(2x) + 日期/开机时长；右半三小格：本机 / 电量 / 未读
有未读 → 有未读态   大格恒反显：最新未读的发件人+正文前两行+时刻；右半：APRS / - / RSSI / SNR
● 短按 → 收件箱态   顶栏反显（发件人 + n/N）；正文两行；元信息两行（时刻 RSSI / 路径 CRC）
▲▼ 滚动；● 短按标已读并前进；● 长按 620ms 退出；未读清零自动回待机
```

整个 UI 只有这三个屏幕态（design.md §6）；设置与诊断类二级页本规范尚未覆盖。

自检可以直接验证导航路径（`--keys` 是按键序列：1=上 2=下 3=确定 4=长按确定）：

```powershell
.\bbcall_sim.exe --selftest --clock 51960 --wallclock 3,9,16 --mycall BG5BLH --batt 2 --demo --out a.bmp   # 有未读态（14:26 / 周三 9/16）
.\bbcall_sim.exe --selftest --demo --keys 3 --out b.bmp                                                  # ● 打开收件箱
.\bbcall_sim.exe --selftest --demo --keys 3,2 --out c.bmp                                                # ▼ 滚正文/翻条
.\bbcall_sim.exe --selftest --demo --keys 3,3,3,3 --out d.bmp                                            # 连按 ● 全部标已读 → 回待机
.\bbcall_sim.exe --keymap                                                                                # 键盘映射自检（6 项）
```

逐屏读屏校验（`tools/verify_ui.py`，从原型字表逐像素比对，差异必须为 0）：

```powershell
python ..\..\tools\verify_ui.py a.bmp unread   # 规格：idle|unread|inbox|inbox2
```

### 自检与数据

```powershell
cd simulator\build-win
.\bbcall_sim.exe --selftest --clock 51960 --wallclock 3,9,16 --mycall BG5BLH --batt 2 --demo --screen idle --out idle.bmp
.\bbcall_sim.exe --selftest --demo --screen inbox --out inbox.bmp
.\bbcall_sim.exe --selftest --replay ..\..\tools\sample_aprs_log.txt --screen inbox --out replay.bmp
.\bbcall_sim.exe --selftest --wav ..\..\tools\test_aprs_144.wav --screen inbox --out wav.bmp
```

`--screen` 可取 `idle` / `unread` / `inbox` / `pattern`；不给 `--screen` 时按未读数自动选择。
`--clock SEC` 固定设备时钟（决定时钟与消息时刻显示）；`--wallclock W,M,D` 模拟 RTC。

### 字体（Fusion Pixel 12px/10px）

三态界面只用 **Fusion Pixel** 字模：12px 用于正文/数值，10px 用于小格标签，
共 374 字形（95 ASCII + 279 汉字），由 `tools/gen_fusion_font.py` 从原型
`bbcall-aprs-screen-states.html` 内嵌字表提取，生成
`firmware-stm32porject/Core/Inc/fusion_font.h`（16.4 KB 常量数据）。

**单源共用**：`ui_harness.c/h` 与 `fusion_font.h` 的真身都在固件
`firmware-stm32porject/Core/` 下，模拟器构建（`build_win.ps1` / Makefile / CMake）
直接编译固件那份，不再保留副本——模拟器看到的就是真机跑的代码。
规范与许可见 [design.md](../design.md) §3.5；与旧 `gen_font.py` ASCII 字模混用不允许。

### 统一收件箱（三态模型的主屏；数据规则参考 GOGUFW）

UI v2.0（2026-09-16）把界面收敛为**三态**：待机 / 有未读 / 收件箱，收件箱是主功能屏
（版面与数据规则见 [design.md](../design.md) §6）。更早的 v2.0（2026-09-14）曾把 v1 并存的
HEARD 台站列表与 MESSAGES 消息列表合并为一个统一收件箱——消息 / 位置 / Mic-E / 其它帧同列，
类型用 `M/P/C/X` 标注；该结构沿革保留作历史记录。数据规则参考
[Gogu-Qs/GOGUFW-UV-K1-Messenger](https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger)
（Apache-2.0，UV-K1 / UV-K5 V3 定制固件，同样是 128x64 单色 LCD）。
采用的规范与取舍都记在根目录 [design.md](../design.md) §11（唯一权威规范）。

- 最新在上；消息时刻按 `rx_ms` 显示 `HH:MM:SS`；未读 `*` 行首；
- ackNNN 送达确认只计数、不进收件箱；
- **本项目仅接收**：界面不提供任何发射入口（没有 SEND / REPLY / Resend）；
- `simulator/src/msg_store.c` 数据模型（Inbox 16 / Sent 8 / Drafts 8，正文 36 字符，
  按 `(from,id)` 去重，`ackNNN` 更新送达状态并记录 ACK 来源）保留给将来双向能力复用，
  三态 UI 起不再编译该文件（分流逻辑已内联进 `ui_feed_ax25()`）。

RSSI/SNR 只在有标定注入时显示：用 `ui_set_radio_stats()` 在入箱前注入、随条目捕获；
日志回放的 `R19=` 原始寄存器值未标定，不注入（design.md §12.2），收件箱元信息处显示 `--`。
## 关于屏幕方向与列偏移（实板结论）

**安装方向**：本机面板 180° 安装，等于上下 + 左右一起翻，所以 `lcd_init()` 发 `0xA0` + `0xC8`
（正装是 `0xA1` + `0xC0`）；`LCD_COL_OFFSET` 随之由 4 变 0。开关是 `bbcall_cfg.h` 的 `LCD_MOUNT_180`。
模拟器在渲染时同样并入这个安装方向（`s_com_rev ^ LCD_MOUNT_180` 等），所以预览与实机一致。

模组是 132 列驱动 + 128 列面板：固件 `lcd_flush()` 从 `LCD_COL_OFFSET`（默认 4，见 `bbcall_cfg.h`）指定的
列开始写，实板画面才不会整体左偏 4 像素。模拟器同样按 132 列寻址（可见列 = 芯片列 - 偏移），
两边相消后预览与实机一致。

### SEG 反向

本机 LCD 模组的 SEG 走线是反的：固件 `lcd_init()` 发 `0xA0`（SEG 正常）+ `0xC0`（COM 正常）时，
实板会整屏**左右镜像**；改成 **`0xA1` + `0xC0`** 后实板画面正常（屏焊上后实测）。

模拟器要模拟的是同一块屏，所以 `lcd_sim.c` 里面板接线默认带 SEG 反向（`s_panel_flip = 1`）：
固件的 `0xA1` 与它相消，预览画面与实机一致，也与既有像素校验一致。按 `F3` 可切回理想面板对比。

（早期没有实板时按"理想面板"写成 `0xA0`，模拟器两边都在理想侧所以看着是对的。常见模板是
`0xA1`+`0xC8`（成对反向＝180°）或 `0xA0`+`0xC0`；`0xA1`+`0xC0` 只剩左右镜像，
正好抵消这块模组的反接。）

## 三个已踩过的构建坑（脚本里已处理）

| 现象 | 原因 | 处理 |
|---|---|---|
| `SDL_platform.h:265: error: ';' expected (got "SDL_GetPlatform")` | TCC(x86_64) 把 `__cdecl` 当普通标识符；SDL `begin_code.h` 在 `__WIN32__ && !__GNUC__` 时把 `SDLCALL` 展开成 `__cdecl` | 加 `-D__cdecl=` |
| `libSDL2.dll.a: error: invalid object file` | TCC 链接器解析不了新版 MinGW 的 GNU 导入库 | 直接链接 `bin/SDL2.dll`（TCC 读 DLL 导出表自建导入） |
| `fatal error: SDL.h: No such file or directory` | PowerShell 会把 `-I"路径"` 的引号吃掉 | 路径不含空格时用 `-I路径` 不加引号 |

## 其它构建方式（需自备工具链）

`build_win.ps1` 之外的两种方式，供已经装了 MSYS2 / vcpkg / w64devkit 的机器使用。

### CMake

```bash
cmake -B build -S simulator
cmake --build build
./build/bbcall_sim --scale 4
```

**MSYS2 / MinGW-w64**：`pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cmake`
**vcpkg**：`vcpkg install sdl2:x64-windows` 后按 `-DCMAKE_TOOLCHAIN_FILE=...` 配置

### Makefile（需要 gcc + sdl2-config）

```bash
cd simulator
make
make run        # 或 ./bbcall_sim --scale 4
make selftest   # 无窗口自检，生成 sim_selftest.bmp
```

## SDL2 依赖说明

本仓库在 `third_party/sdl2/` 内置了 SDL2 2.32.10（x86_64-w64-mingw32，zlib 许可）：

```text
third_party/sdl2/
  include/SDL2/        # 头文件
  bin/SDL2.dll         # 运行库（构建后拷到 exe 同目录）
  lib/libSDL2.dll.a    # MinGW 导入库（供 CMake / Makefile 方式使用）
  LICENSE.txt          # zlib 许可
  SOURCE.txt           # 来源与版本
```

`third_party/` 已被 `.gitignore` 排除（与 TinyCC 同理，属于本机免安装工具链）。
`build_win.ps1` 会依次尝试环境变量 `SDL2_DIR`、`third_party\sdl2\`、
`%TEMP%\bbcall_sim_tools\sdl2\SDL2-*\x86_64-w64-mingw32\`。

## 实现说明

- `lcd_sim.c` 实现 ST7567 命令/数据显示状态机：页地址 `0xB0-0xB7`、列地址低/高半字节
  `0x00-0x0F` / `0x10-0x1F`、显示开关 `0xAE/0xAF`、反显 `0xA6/0xA7`、全亮 `0xA4/0xA5`、
  起始行 `0x40-0x7F`、SEG 方向 `0xA0/0xA1`、COM 方向 `0xC0/0xC8`、复位 `0xE2`；
- `ui_harness.c` 是三态界面（待机 / 有未读 / 收件箱）与按键状态机，解析复用 `ax25.c`、`aprs.c`；
- `sim_feed.c` 是三种数据源（WAV / 日志 / 示例）；
- `sim_hal.c` 提供最小 HAL/GPIO/延时桩，让固件 .c 能在 PC 编译；
- 真机固件构建不受影响：`LCD_SIM` 只在模拟器构建中定义。