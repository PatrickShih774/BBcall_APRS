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

| 按键 | 功能 |
|---|---|
| ↑ / ↓ | 收件箱上下选择；详情页翻页 |
| Enter | 打开选中的消息 |
| Backspace | 返回（详情→收件箱→待机） |
| Delete | 删除选中消息（反显弹窗二次确认，OK=删除 / BACK=取消） |
| T | 测试图案（棋盘格 + 8x16/6x8 两套字体样张） |
| M | 收件箱 |
| S | 待机界面 |
| I | 反显 |
| B | 背光开关 |
| F3 | 切换面板 SEG 方向（见下文"关于水平镜像"） |
| F12 | 截屏 `lcd_sim.bmp` |
| Esc | 退出 |

## 三种数据来源

| 来源 | 命令 | 走到的代码 |
|---|---|---|
| 内置示例 | `--demo` | `ax25.c` + `aprs.c` |
| 串口日志回放 | `--replay FILE` | 日志里 `[RAW] hex=` → `ax25.c` + `aprs.c` |
| WAV 音频解调 | `--wav FILE` | `modem.c` → `ax25.c` → `aprs.c` |

WAV 路径是**完整的固件解码链路**：48 kHz/16bit PCM 经 5 点滑动平均降到 9600 Hz，
映射成 12bit ADC 码值（中心 2048、幅度 ±800）后逐点调用固件入口 `modem_adc_sample()`，
再由 `modem_get_frame()` 取出帧。这就是把 STM32 的 ADC 中断源换成音频文件而已。

实测 `tools/test_aprs_144.wav`（`:BG5BLH   :Hello APRS 144.640`）：

```text
[sim] WAV ...: 48000 Hz / 1 ch / 24000 帧 (0.500 s)
[sim] 解调统计: mark=1691 space=3102 other=0  FIX=0 FIX2=0 REP=0  -> 收帧 1
[sim] 收件箱 1 条 / 累计收到 1 帧（重复抑制 15）
```

`收帧 1 / 重复抑制 15` 是预期行为：16 路并行相位走廊会各解出同一帧，
模拟器与固件 `bbcall_app.c` 一样按「同源同内容 60s 内只收一次」抑制。

## 界面：复古寻呼机（BB 机）风格

**设计规范见仓库根目录 [../UISkill.md](../UISkill.md)**：适用范围声明、Design Read 与三档 Dials、
Web 规则到 1-bit 的等价物、唯一 chrome 系统、图标家族、屏幕版面模板、硬规则、Pre-flight 检查表、
真机移植清单。本节只记录模拟器侧的实现要点。

- 界面状态机：`simulator/src/ui_harness.c`（模型 + 全部屏幕 + 按键）；
- 绘图原语与两套字模：固件 `firmware-stm32porject/Core/Src/lcd_st7567.c`（真机与模拟器同一份）；
- 字体生成：`tools/gen_font.py`（8x16）与 `tools/gen_font.py --small`（6x8，21 字符/行）。

### 屏幕

| 屏幕 | 内容 |
|---|---|
| `boot` | 2 倍放大 "BBCALL" + 副标题 + 固件版本 |
| `home` | 状态栏 + 16x32 大时钟（开机计时，左侧 `UP` 标注）+ 频率/RX 数 + 最近一条来源与位置 |
| `menu` | 6 项：8x8 图标 + 名称 + 右对齐数值；选中行整行反显 |
| `inbox` | 6 行，最新在上；每行 = 呼号 + 类型 + 摘要 + 未读 `*` |
| `detail` | 标题 + 5 行正文 + 页脚（页码 / `RELAY <中继路径>` / `FIX` / `REP`） |
| `radio` | 频率 + S 表 + RSSI / SNR / AFC / EXN + RX / DUP + 音频状态 |
| `about` | 版本与硬件信息 |
| `confirm` | 删除确认：填充块 + 内嵌 1px 框 + 反显文字 |
| `pattern` | 棋盘格 + 两套字体全字符样张 |
| `messenger` | Messenger 启动器：INBOX / HEARD / COMPOSE / SENT，选中项右侧 24x24 大图标 |
| `msginbox` | 收件箱 6 行，最新在上：`*` 未读 + 正文预览 + 年龄（`NOW`/`12m`/`3h`） |
| `msgsent` | 已发 6 行，行首 `+` 已确认 / `x` 失败 / `-` 待确认 |
| `msgread` | 阅读页：`FROM:`/`TO:` + 年龄 + 正文 4 行 + 页脚 `REPLY`/`DEL` |
| `compose` | 组包页：`NEW MESSAGE` + `n/36` 计数 + 光标，页脚 `SAVE DRAFT` / `TX OFF` |

### 按键

| 键 | 作用 |
|---|---|
| ↑ / ↓ | 菜单或列表上下移动；详情页翻页 |
| Enter | 打开（主页/菜单进入下一级；列表进入详情并标记已读） |
| Backspace | 返回上一级（详情 -> 列表 -> 菜单 -> 主页） |
| Delete | 删除选中消息（反显弹窗二次确认） |
| M | 菜单（菜单第 1 项进入 Messenger） |
| 字符键 | 组包页输入文字（SDL 文本输入；Backspace 删字符，空时退出） |
| S | 主页 |
| T | 字体样张 |
| I / B | 反显 / 背光开关 |
| F3 | 切换面板 SEG 方向（对比两种接线） |
| F12 | 截屏 `lcd_sim.bmp` |
| Esc | 退出 |

### 自检与数据

```powershell
cd simulator\build-win
.\bbcall_sim.exe --selftest --scale 3 --clock 4337 --screen home --out home.bmp
.\bbcall_sim.exe --selftest --replay ..\..\tools\sample_aprs_log.txt --screen radio --out radio.bmp
.\bbcall_sim.exe --selftest --wav ..\..\tools\test_aprs_144.wav --screen detail --out wav.bmp
```

`--screen` 可取 `boot` / `home` / `menu` / `inbox` / `detail` / `radio` / `about` / `confirm` / `pattern`；
`--clock SEC` 固定大时钟（截图用）。

### Messenger（参考 GOGUFW）

消息界面的版面参考 [Gogu-Qs/GOGUFW-UV-K1-Messenger](https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger)
（Apache-2.0，UV-K1 / UV-K5 V3 定制固件，同样是 128x64 单色 LCD）。
采用的规范与有意偏离都记在根目录 [UISkill.md](../UISkill.md) 第 11 节。

- 数据模型：`simulator/src/msg_store.c`（Inbox 16 / Sent 8 / Drafts 8，正文 36 字符，
  按 `(from,id)` 去重，`ackNNN` 更新送达状态并记录 ACK 来源）；
- 收到的 APRS 消息会自动进 Messenger 收件箱，同时仍进 HEARD 条目列表；
- **本项目仅接收**：`COMPOSE` 只存草稿，页脚明确 `TX OFF`；`SENT` 用 `--demo` 注入演示数据。

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