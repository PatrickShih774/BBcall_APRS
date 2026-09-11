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
## STMicroelectronics STM32 HAL / CMSIS

- 位置：`firmware-stm32porject/Drivers/`
- 许可：随 STM32CubeIDE 工程生成，目录内附有 `LICENSE.txt`（STM32 HAL/CMSIS 的 BSD-3-Clause 等条款）。
- 合规说明：保留 `Drivers/` 下的原始版权与许可文件，不修改许可声明。

## BK4802P.pdf（参考数据手册）

- `BK4802P.pdf` 是 Beken 的 BK4802P 数据手册，作为本项目的硬件参考手册保留在仓库中。
- 版权归 Beken 所有，仅供学习与开发参考；如需商用或再分发请遵循厂商授权。
- 若权利人提出要求，可将其从仓库中移除。