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

### 设计依据

按 `ui-design` + `taste-skill` 的 overlay 契约执行。先说清适用范围：这两个 skill 的目标产物是
HTML/CSS/JS（Track A/B、组件库、WCAG、Core Web Vitals、GSAP），**工程半边不适用于 128x64 1-bit LCD**；
这里只执行 taste 的三块内容，并把 Web 工程护栏换成嵌入式等价物：

| Web 原规则 | 本项目的等价物 |
|---|---|
| 颜色对比 / 单一强调色 | 只有开与关两态，层级靠**面积、墨量、反显**；"强调色"只能是反显 |
| `prefers-reduced-motion` | **刷新成本与闪烁**：只重绘必要区域，不做全屏闪 |
| Dark mode | 屏的两种极性：**背光正显 / 反显负片**，两者都设计 |
| Breakpoints / 移动端折叠 | 固定 128x64 网格；密度档 = 6x8 与 8x16 |
| 禁止手搓 SVG 图标 | 必须自绘 1-bit 图标，等价纪律：**统一 8x8 网格、统一 1px 线宽、单一图标家族** |

**Design Read**：复古手持寻呼机设备 UI，用户是单人业余无线电操作者，采用 90 年代末点阵寻呼机
（Motorola Advisor 一类）的视觉语言，倾向 1-bit 单色系统：状态图标栏 + 反显选择条 + 大字号时钟。

**Design Dials**：`DESIGN_VARIANCE 6 / MOTION_INTENSITY 2 / VISUAL_DENSITY 7`。
密度 7 按 taste 第 7 节要求**用 1px 细线分隔数据、不用卡片盒**；运动强度 2 只保留一处有动机的动画
（大时钟冒号闪烁，作为设备存活反馈），其余全部静态。

**Anti-default**：1-bit 世界最偷懒的默认是"什么都套一个 1px 方框"（等价于 Web 的卡片默认）。
本项目改用**单一 chrome 系统**，全屏只此一套，不在内容里再套框：

```text
y0..7    顶部状态栏（反显）：左侧屏幕名，右侧 [信号格] 3px [静音] 3px [未读数] 3px [信封]
y8       1px 细线
y10..56  内容区，6x8 行网格 y = 10 / 18 / 26 / 34 / 42 / 50
x124..127 滚动轨（仅列表溢出时出现）
```

选择态一律用**反显条**（寻呼机的原生语言），而不是加箭头或换色。

### 两套字体

| 字体 | 尺寸 | 容量 | 用途 |
|---|---|---|---|
| `font8x16.h` | 8x16 | 16 字符/行 | 标题、大时钟（2 倍放大 = 16x32） |
| `font6x8.h` | 6x8 | **21 字符/行** | 状态栏、列表、正文、菜单 |

两套字体、`lcd_fill_rect` / `lcd_hline` / `lcd_vline` / `lcd_rect` / 放大绘制都在固件
`lcd_st7567.c` 里，**模拟器与真机同一份代码**。

### 屏幕

| 屏幕 | 内容 |
|---|---|
| `boot` | 开机画面：2 倍放大 "BBCALL" + 副标题 + 固件版本 |
| `home` | 状态栏 + **16x32 大时钟**（无 RTC，为主机计时，左边标 `UP` 明示）+ 频率/RX 数 + 最近一条来源与位置 |
| `menu` | 6 项菜单，每项 = 8x8 图标 + 名称 + 右对齐数值；选中行整行反显；右侧滚动轨 |
| `inbox` | 6 行列表，**最新在上**；每行 = 呼号 + 类型 + 摘要 + 未读 `*`；标题栏显示位置与未读数 |
| `detail` | 标题 + 5 行正文 + 页脚（页码 / `RELAY <中继路径>` / `FIX` / `REP`） |
| `radio` | 电台页：频率 + S 表 + RSSI / SNR / AFC / EXN + RX / DUP 计数 + 音频与静音状态 |
| `about` | 版本与硬件信息 |
| `confirm` | 删除确认：填充块 + 内嵌 1px 框 + 反显文字（寻呼机原生做法） |
| `pattern` | 棋盘格 + 两套字体全字符样张（换字体/换屏后一眼看出缺字） |

### 图标家族（统一 8x8、1px 线宽、自绘）

`信号格`（4 柱高度 3/5/7/8，按 S 表点亮）、`信封`、`静音喇叭`、`地图针`、`天线`、
`对比度`、`信息 i`、`电源`。

`radio` 页的 RSSI / SNR / AFC / EXN 与状态栏信号格都来自日志里的**真实** `S=` 与 `R19=` 行，
不是编出来的假数据。

### 行为细节

- 未读模型：新入箱记未读，Enter 打开后置为已读，状态栏与菜单数值随之更新；
- 删除两步走：Delete 弹确认，OK 才真删（同时标记已读），BACK 取消；
- 收件箱容量 24，满时丢弃最旧；列表最新在上；滚动窗口跟随选中项；
- 时间戳：日志有 `[T=..ms]` 时显示时间，没有时退化为会话序号 `#nn`，不显示假的 `0s`；
- 所有居中/右对齐都吸附字符栅格（8 或 6 的整数倍），否则字会落在半个字符上、点阵发糊。

### 已知留白（诚实标注）

- **电池图标位保留但未启用**：本板没有电池采样电路，与其画一个假电量，不如留空。
  接上分压到 ADC 后再补；
- 大时钟是**开机计时**而非墙上时钟（无 RTC 芯片），左侧 `UP` 即为此意；
- `Contrast` 菜单项在真机上会改 ST7567 `0x81` 的值，模拟器里是空操作。

### 自定义设计脚本

`tools/gen_font.py` 可重新生成两套字体：

```bash
python tools/gen_font.py                      # 8x16 大字号
python tools/gen_font.py --small out.h        # 6x8 小字号
```
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