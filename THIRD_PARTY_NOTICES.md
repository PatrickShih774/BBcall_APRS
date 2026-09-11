# 第三方组件与许可声明

本项目自身以 **GPL-3.0** 发布（见根目录 `LICENSE`）。以下是参考/使用的第三方项目与许可。

## MM-Radio

- 仓库：https://github.com/doublehan07/MM-Radio
- 许可：**BSD 2-Clause License**，Copyright (c) 2024, Han Zhang
- 用途：本项目的应用参考与工程底座。工程结构、BK4802 驱动思路、部分寄存器初始化序列参考/移植自该项目。
- 许可文本：`licenses/MM-Radio-BSD-2-Clause.txt`
- 合规说明：BSD-2-Clause 是宽松许可，允许并入 GPL-3.0 项目；分发时保留上述版权声明、条件与免责声明即可。

## BG5ESN FMO-Radio-Module-BK4802-V2.00

- 仓库：https://github.com/BG5ESN/FMO-Radio-Module-BK4802-V2.00
- 许可：**MIT License**，Copyright (c) 2025 BG5ESN
- 用途：BK4802 频率字（Frac-N / Ndiv / IF）计算方式的参考。
- 许可文本：`licenses/FMO-MIT.txt`
- 合规说明：MIT 与 GPL-3.0 兼容，保留版权与许可声明即可。

## VP-Digi

- 仓库：https://github.com/sq8vps/vp-digi
- 许可：**GPL-3.0**
- 用途：AFSK/AX.25/APRS 解调方案的资料参考（未直接复制代码）。
- 合规说明：与本项目 GPL-3.0 相同，兼容。

## BG7QKU（STM32_SIMPLE_CONTROL_BK4802N 等）

- 仓库：https://github.com/BG7QKU/STM32_SIMPLE_CONTROL_BK4802N
- 许可：**仓库中未声明 LICENSE 文件**（默认保留所有权利）。
- 用途：仅作为 BK4802 寄存器行为的资料参考，不直接复制其代码。
- 合规说明：由于未声明开源许可，本项目不把其代码纳入源码树；如需引用其代码，应先取得作者授权。

## GOGUFW-UV-K1-Messenger（UI 参考）

- 仓库：https://github.com/Gogu-Qs/GOGUFW-UV-K1-Messenger
- 许可：**Apache License 2.0**
- 用途：Messenger 界面族的**设计参考**。其目标硬件泉盛 UV-K1 / UV-K5 V3 使用同样 128x64 的单色 LCD，
  因此本项目参考了它的消息模型与版面参数：正文 36 字符上限、Inbox 16 / Sent 8 / Drafts 8、
  `NOW`/`12m`/`3h` 年龄列、未读 `*`、送达 `+`/`x`/`-`、按 `(from,id)` 去重、`ackNNN` 送达确认、
  点状分隔线（1 实 3 空）、右对齐计数留安全边距、启动器选中项右侧大图标。
- **未复制其代码**：本项目的实现（`simulator/src/msg_store.c`、`ui_harness.c` 的 Messenger 段）
  按本项目自己的 chrome 系统与字模独立编写；采用与偏离逐条记录在 `UISkill.md` 第 11 节。
- 合规说明：Apache-2.0 与 GPL-3.0 兼容；本项目未纳入其源码，仅作设计参考并在文档中标注出处。
  如后续需要直接复用其代码，应保留 Apache-2.0 许可与 NOTICE 要求。
## X11 misc-fixed 点阵字体（ASCII 字模字源）

- 位置：`tools/bdf/6x9.bdf`、`tools/bdf/7x13.bdf`
- 许可：**公有领域**。BDF 内自带声明 `COPYRIGHT "Public domain font.  Share and enjoy."`
- 用途：`firmware-stm32porject/Core/Inc/font6x8.h` 与 `font8x16.h` 由它们生成
  （工具 `tools/gen_font.py`，只取 ASCII 0x20..0x7F 并重排进我们的单元格）
- 副本取自 [olikraus/u8g2](https://github.com/olikraus/u8g2) 的 `tools/font/bdf/`
  （u8g2 库本身是 BSD-2；字体许可以各 BDF 内声明为准，这两个是公有领域）
- 合规说明：公有领域，无附加条件，与 GPL-3.0 无冲突

## joaquimorg/UV-KX（字模做法参考）

- 仓库：https://github.com/joaquimorg/UV-KX
- 许可：**仓库未声明 LICENSE**（默认保留所有权利）
- 用途：**仅借鉴做法**。它用 BDF 点阵字 + u8g2 的 `bdfconv` 转成紧凑数组，
  并用 `-m "32-95"` 只取需要的字符（5x7 成品仅 492 字节）。本项目采用同样的"点阵 BDF 而非
  TrueType 栅格化"思路，但**自己实现 BDF 解析**，不引入 u8g2 依赖。
- **未使用其代码，也未使用其字源**：它的 `fonts_icons/` 里是 Pixies（Randy Humphries）、
  Uni0553/Uni0563（miniml.com, Craig Kroeger）等个人字体，版权归各自作者，且仓库无许可声明，
  因此本项目改用公有领域的 X11 misc-fixed。
## GNU Unifont（中文字模字源）

- 项目：https://unifoundry.com/unifont/ ，本仓库使用 `unifont-16.0.01.hex`
- 许可：**OFL-1.1 或 GPLv2-or-later 双许可**（自 2013 起）。两种都与本项目 GPL-3.0 兼容。
- 用途：`firmware-stm32porject/Core/Inc/cn_font_data.h` 里的 16x16 中文字形子集由它生成。
- 许可文本：`licenses/GNU-Unifont-OFL.txt`
- 生成方式：`tools/gen_cn_font.py`（字形为纯点阵数据，未修改）

> **不要改用 WenQuanYi Bitmap Song**：它是 GPL v2（仅此一版）+ 字体嵌入例外，
> 与本项目 GPL-3.0 不兼容（GPLv2-only 无法并入 GPLv3）。同理适用于其它 GPLv2-only 的点阵字库。

## EthanYan6/Dondji（中文字库方案参考）

- 仓库：https://github.com/EthanYan6/Dondji ，许可：**Apache-2.0**
- 用途：中文显示方案的**设计参考**。其字库布局为
  `[位图][Unicode 索引 4B/项 升序][拼音表][版本字节]`，放在**外部 SPI Flash**，固件只保留布局常量；
  本项目沿用同一形状（见 `tools/gen_cn_font.py` 与 `PLAN.md` 中文显示一节）。
- **未复制其代码，也未使用其字模数据**（其字源为 WQY Bitmap Song，许可与本项目不兼容）。
- 合规说明：Apache-2.0 与 GPL-3.0 兼容；仅作设计参考并在文档标注出处。
## STMicroelectronics STM32 HAL / CMSIS

- 位置：`firmware-stm32porject/Drivers/`
- 许可：随 STM32CubeIDE 工程生成，目录内附有 `LICENSE.txt`（STM32 HAL/CMSIS 的 BSD-3-Clause 等条款）。
- 合规说明：保留 `Drivers/` 下的原始版权与许可文件，不修改许可声明。

## BK4802P.pdf（参考数据手册）

- `BK4802P.pdf` 是 Beken 的 BK4802P 数据手册，作为本项目的硬件参考手册保留在仓库中。
- 版权归 Beken 所有，仅供学习与开发参考；如需商用或再分发请遵循厂商授权。
- 若权利人提出要求，可将其从仓库中移除。