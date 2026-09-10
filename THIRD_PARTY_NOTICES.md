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

## STMicroelectronics STM32 HAL / CMSIS

- 位置：`firmware-stm32porject/Drivers/`
- 许可：随 STM32CubeIDE 工程生成，目录内附有 `LICENSE.txt`（STM32 HAL/CMSIS 的 BSD-3-Clause 等条款）。
- 合规说明：保留 `Drivers/` 下的原始版权与许可文件，不修改许可声明。

## BK4802P.pdf（重要提醒）

- `BK4802P.pdf` 是 Beken 的数据手册，封面标注 *Beken Confidential / covered under NDA*。
- 该文件目前被提交到公开仓库，存在版权/NDA 合规风险。
- 建议：从公开仓库移除（本地保留），不要在 Release 附件中分发；需要公开说明时只给厂商官方链接或自行获取说明。
