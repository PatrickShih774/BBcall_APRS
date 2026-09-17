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
- **未复制其代码**：本项目最初版的实现（`simulator/src/msg_store.c`、`ui_harness.c` 的 Messenger 段）
  按本项目自己的 chrome 系统与字模独立编写；采用与偏离逐条记录在 `../docs/design.md` §11
  （原 `UISkill.md` 第 11 节，已并入 design.md v2.0）。
- 合规说明：Apache-2.0 与 GPL-3.0 兼容；本项目未纳入其源码，仅作设计参考并在文档中标注出处。
  如后续需要直接复用其代码，应保留 Apache-2.0 许可与 NOTICE 要求。

## Fusion Pixel Font（UI 字模字源）

- 仓库：https://github.com/TakWolf/fusion-pixel-font ，Copyright (c) 2022, TakWolf
- 许可：**SIL Open Font License 1.1**（字体部分；构建程序为 MIT，与本项目无关）
- 用途：UI v2.0 三态界面的全部字模（Fusion Pixel 12px/10px 单宽版，374 字形子集），
  经 `tools/gen_fusion_font.py` 从原型内嵌字表提取为 `firmware-stm32porject/Core/Inc/fusion_font.h`
  （字形为纯点阵数据，未修改）。
- 许可文本：`licenses/Fusion-Pixel-OFL.txt`（上游各字型许可见该仓库 `LICENSES/` 目录；
  本项目只提取位图子集，未随包再分发）。
- 合规说明：OFL-1.1 允许嵌入与再分发（含修改版），与 GPL-3.0 无冲突；保留本声明与许可文本即可。

## EthanYan6/Dondji（中文字库方案参考）

- 仓库：https://github.com/EthanYan6/Dondji ，许可：**Apache-2.0**
- 用途：中文显示方案的**设计参考**。其字库布局为
  `[位图][Unicode 索引 4B/项 升序][拼音表][版本字节]`，放在**外部 SPI Flash**，固件只保留布局常量；
  该方案仅作为历史调研记录；当前仓库未包含其代码、布局实现或字模数据。
- **未复制其代码，未使用其字模数据**（其字源为 WQY Bitmap Song，许可与本项目不兼容）。
- 合规说明：Apache-2.0 与 GPL-3.0 兼容；仅作设计参考并在文档标注出处。

## STMicroelectronics STM32 HAL / CMSIS

- 位置：`firmware-stm32porject/Drivers/`
- 许可：随 STM32CubeIDE 工程生成，目录内附有 `LICENSE.txt`（STM32 HAL/CMSIS 的 BSD-3-Clause 等条款）。
- 合规说明：保留 `Drivers/` 下的原始版权与许可文件，不修改许可声明。

## docs/BK4802P.pdf（参考数据手册）

- `docs/BK4802P.pdf` 是 Beken 的 BK4802P 数据手册，作为本项目的硬件参考手册保留在仓库中。
- 版权归 Beken 所有，仅供学习与开发参考；如需商用或再分发请遵循厂商授权。
- 若权利人提出要求，可将其从仓库中移除。