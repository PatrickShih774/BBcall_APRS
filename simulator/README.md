# BBcall_APRS PC LCD 模拟器（SDL2）

用 SDL2 模拟 ST7567 128×64 单色点阵，直接编译固件里的 `lcd_st7567.c` 绘图代码，
无需烧录 STM32 即可看到屏幕效果。

## 依赖

- CMake ≥ 3.16
- C 编译器（MinGW-w64 / MSVC）
- SDL2 开发库

### 安装 SDL2（任选一种）

**MSYS2 / MinGW-w64**

```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cmake
```

**vcpkg**

```powershell
vcpkg install sdl2:x64-windows
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

**便携工具链（无管理员权限）**

1. 下载 w64devkit（含 gcc/make）：https://github.com/skeeto/w64devkit/releases
2. 下载 SDL2-devel-2.x.x-mingw.tar.gz：https://github.com/libsdl-org/SDL/releases
3. 解压后把 SDL2 的 `include`、`lib`、`bin` 路径配好，使用 gcc 直接编译（见 Makefile 示例），或安装 CMake 后按上面的 vcpkg 方式。

## 构建与运行

```bash
cmake -B build -S simulator
cmake --build build
./build/bbcall_sim --scale 4
```

Windows MinGW 产物为 `build/bbcall_sim.exe`。

无窗口自检（生成一张 BMP，适合 CI/无显示器环境）：

```bash
./build/bbcall_sim --selftest
```

## 按键

| 按键 | 功能 |
|---|---|
| ↑ / ↓ | 上/下（收件箱选择） |
| Enter | 确定/打开消息 |
| Backspace | 返回待机 |
| T | 测试图案（棋盘格/斜线/文字） |
| M | 消息详情页 |
| S | 待机界面 |
| I | 反显 |
| B | 背光开关 |
| F12 | 截屏 `lcd_sim.bmp` |
| Esc | 退出 |

## 说明

- 模拟器实现 ST7567 的命令/数据显示状态机（页/列地址、显示开关、反显、全亮、起始行、SEG/COM 方向），与真机页位序一致；
- `lcd_st7567.c` 的绘图逻辑、`fb[1024]`、字体完全复用固件代码；
- 固件构建不受影响：`LCD_SIM` 只在模拟器 CMake 中定义。