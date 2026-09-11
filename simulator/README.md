# BBcall_APRS PC LCD 模拟器（SDL2）

用 SDL2 模拟 ST7567 128×64 单色点阵，**直接编译固件里的代码**：

```text
lcd_st7567.c  ST7567 驱动 / 绘图 / 8x16 字体
ax25.c        HDLC 去填充 / CRC-16/X.25 / 地址解析 / 纠错
aprs.c        APRS 消息 / 位置 / Mic-E 解析
modem.c       1200 baud Bell202 AFSK 解调（16 相位 + 9 条跳变对齐）
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

# 无窗口自检：把某个界面渲染成 BMP
.\bbcall_sim.exe --selftest --wav ..\..\tools\test_aprs_144.wav --screen detail --out detail.bmp
```

### 命令行参数

```text
--scale N        放大倍数 1..12（默认 4）
--selftest       无窗口（SDL dummy 驱动）渲染一帧并写出 BMP
--out FILE       自检输出文件名（默认 sim_selftest.bmp，按 scale 放大）
--screen NAME    boot | standby | inbox | detail | confirm | pattern
--wav FILE       WAV -> modem.c 解调 -> 收件箱
--replay FILE    串口日志 [RAW] len=.. hex=.. 回放到收件箱
--demo           注入内置示例帧
-h / --help      帮助
```

### 按键

物理键与数字键**等价**（数字键用的是内部键码，与 `--keys` 的编号一致）：

| 物理键 | 数字键 | 内部键码 | 作用 |
|---|---|---|---|
| ↑ / ↓ | `1` / `2` | 1 / 2 | 列表或菜单上下移动 |
| Enter | `3` | 3 | 打开 / 确认 |
| Backspace | `4` | 4 | 返回上一级；**在待机页或收件箱上进入二级菜单** |
| T | `5` | 5 | 字体样张 |
| S | `7` | 7 | **待机页**（一级屏：大时钟 + 频率 + 计数 + 最近一条） |
| Delete | `8` | 8 | 删除选中消息（反显弹窗二次确认） |
| M | `9` | 9 | **收件箱**（一级屏，主功能） |
| I / B | — | — | 反显 / 背光开关 |
| F3 / F12 / Esc | — | — | 面板 SEG 方向 / 截图 / 退出 |

> 窗口必须先点一下拿到焦点，否则按键不会送进来。
> 键映射本身可自检：`.\bbcall_sim.exe --keymap`（16 项，逐条打印 SDL 键 -> 内部键码）。
> 加这个自检是因为踩过一次坑：文档写"按 7 是待机页"，但键盘当时只绑了字母 S，数字键根本没映射。
### 导航

```text
开机 → boot 闪屏
        ↓
   一级屏（并列，可随时互切）
     ├ STANDBY   待机页（S 键）   大时钟 + 频率 + RX/MSG + 最近一条
     └ MESSAGES  收件箱（M 键）   主功能，开机默认停在这里
            │ BACK
            ▼
     二级菜单 MENU：Heard / Radio / Contrast / Backlight / About
            │ BACK → 回到进入菜单前的那块一级屏（不是固定回某一个）
```

两层的视觉刻意不同：一级屏（STANDBY / INBOX）状态栏右侧有图标群、选中行整行反显；
二级页（MENU 及其子页）状态栏是面包屑（`MENU>HEARD`）且右侧留空、内容缩进 6px、选中用内侧反显条。

自检可以直接验证整条路径（`--keys` 是按键序列：1=上 2=下 3=OK 4=BACK 5=图案 7=待机 8=删除 9=收件箱）：

```powershell
.\bbcall_sim.exe --selftest --demo --scale 2 --out a.bmp                  # 开机即收件箱
.\bbcall_sim.exe --selftest --demo --scale 2 --keys 7 --out b.bmp         # S -> 待机页
.\bbcall_sim.exe --selftest --demo --scale 2 --keys 7,4 --out c.bmp       # 待机 BACK -> 二级菜单
.\bbcall_sim.exe --selftest --demo --scale 2 --keys 7,4,4 --out d.bmp     # 菜单 BACK -> 回待机页
.\bbcall_sim.exe --selftest --demo --scale 2 --keys 4,3 --out e.bmp       # 菜单第 1 项 -> HEARD
.\bbcall_sim.exe --keymap                                                 # 键盘映射自检（16 项）
```
### 自检与数据

```powershell
cd simulator\build-win
.\bbcall_sim.exe --selftest --scale 3 --clock 4337 --screen home --out home.bmp
.\bbcall_sim.exe --selftest --replay ..\..\tools\sample_aprs_log.txt --screen radio --out radio.bmp
.\bbcall_sim.exe --selftest --wav ..\..\tools\test_aprs_144.wav --screen detail --out wav.bmp
```

`--screen` 可取 `boot` / `home` / `menu` / `inbox` / `detail` / `radio` / `about` / `confirm` / `pattern`；
`--clock SEC` 固定大时钟（截图用）。

### 中文字库（布局参考 Dondji）

字库由 `tools/gen_cn_font.py` 生成，布局沿用 [EthanYan6/Dondji](https://github.com/EthanYan6/Dondji)
（Apache-2.0）的形状：`[位图][Unicode 索引 4B/项 升序][版本字节]`，16x16 点阵。

```powershell
python tools\gen_cn_font.py --unifont <unifont.hex> --chars-file tools\cn_chars.txt `
    --out-header firmware-stm32porject\Core\Inc\cn_font_data.h --out-bin tools\cn_font.bin
```

- `build_win.ps1` 检测到 `Core/Inc/cn_font_data.h` 会自动加 `-DCN_FONT_ENABLED=1`；
- 当前子集 107 字、3,853 字节，目标平台实测编译后占 4,028 字节 Flash；
- `--screen cnfont` 逐页看全部字形；About 页显示 `CN FONT <字数>`（未启用则 `CN FONT OFF`）；
- **不要用 WenQuanYi Bitmap Song**：GPL v2 only，与本项目 GPL-3.0 不兼容；默认字源是
  GNU Unifont（OFL-1.1 / GPLv2+ 双许可）。

### Messenger（参考 GOGUFW）

消息界面的版面参考 [Gogu-Qs/GOGUFW-UV-K1-Messenger](https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger)
（Apache-2.0，UV-K1 / UV-K5 V3 定制固件，同样是 128x64 单色 LCD）。
采用的规范与有意偏离都记在根目录 [UISkill.md](../UISkill.md) 第 11 节。

- 数据模型：`simulator/src/msg_store.c`（Inbox 16 / Sent 8 / Drafts 8，正文 36 字符，
  按 `(from,id)` 去重，`ackNNN` 更新送达状态并记录 ACK 来源）；
- 收到的 APRS 消息会自动进 Messenger 收件箱，同时仍进 HEARD 条目列表；
- **本项目仅接收**：界面不提供任何发射入口（没有 SEND / REPLY / Resend）。
  最初照搬 GOGUFW 做过 4 项启动器 + COMPOSE + SENT，实测过于复杂且是死路，已砍成
  `messages` + `msgread` 两屏；Sent 数据结构与 `ackNNN` 分流保留在数据层，打开发射能力后可直接复用。

状态栏信号格与 `radio` 页的数值取自日志里真实的 `S=` 与 `R19=` 行。
`tools/sample_aprs_log.txt` 是压缩版真实日志：每个 `[RAW]` 帧前补上它在原日志中最近一次的
`S=` / `R19=` / `M=` 状态行，所以显示的是真数据而不是编出来的。
## 关于屏幕方向（已修复水平翻转）

固件 `lcd_init()` 原先发送 `0xA1`（SEG/ADC 段反向）+ `0xC0`（COM 正常），
这个混搭会让整屏**左右镜像**，文字全部反着显示；现改为 `0xA0` + `0xC0`，画面正常。

常见 ST7567 模板是 `0xA1`+`0xC8`（成对反向，等于 180°）或 `0xA0`+`0xC0`（都正常）。

模拟器按 ST7567 状态机忠实复现硬件行为，所以固件一改、模拟器画面立刻跟着变正——
可以用它先确认方向再烧片。按 `F3` 仍可临时切换朝向做对比。

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
- `ui_harness.c` 是收件箱/详情/待机界面与按键状态机，解析复用 `ax25.c`、`aprs.c`；
- `sim_feed.c` 是三种数据源（WAV / 日志 / 示例）；
- `sim_hal.c` 提供最小 HAL/GPIO/延时桩，让固件 .c 能在 PC 编译；
- 真机固件构建不受影响：`LCD_SIM` 只在模拟器构建中定义。