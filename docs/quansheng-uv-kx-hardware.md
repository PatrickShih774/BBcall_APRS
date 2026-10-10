# 泉盛 UV-KX 系列硬件与 BK4819 / BK4829 射频芯片对照（参考页）

> **用途**：给"基于对讲机硬件做二次开发/移植固件"的项目当硬件索引与差异清单。
> **数据来源**：社区开源固件源码（GitHub），抓取日期 **2026-10-10**；属于**源码反推**结论，
> 未做数据手册级验证。落锤请拆机看芯片丝印（老 K5 屏蔽罩下是 BK4819，新代对应位号那颗丝印不同）。
> 本页可独立使用，不依赖任何具体固件工程。

---

## 1. 机型与硬件对照

| 机型 | 主控 MCU | 射频收发芯片 | 外部 Flash / 刷机方式 | 社区固件 / 工具 |
|---|---|---|---|---|
| UV-K5 / K5(8) / K6（**V1，老款**） | **DP32G030** | **BK4819** | 无（EEPROM）/ CH340-CH9102 串口线 | DualTachyon 奠基、F4HWN、losehu、egzumer |
| **UV-K5 V3** | **PY32F071** | **BK4829** | 新代固件内置 `py25q16` 驱动（SPI Flash）；K1 系列走 **USB-C (DFU)** | F4HWN Fusion（armel 移植）、hyggx 八荒固件 |
| **UV-K6 V3** | **PY32F071** | **BK4829** | 同上 | hyggx 八荒固件、F4HWN 系 |
| **UV-K1** | **PY32F071** | **BK4829** | 同上；K1 系列 USB-C | armel、hyggx、EthanYan6、knoinok 工具链 |
| **UV-K1(8) v3 "Mini Kong"** | **PY32F071** | **BK4829** | **+2 MB SPI Flash** | 同上 |

**要点**

1. 老代（DP32G030）与新代（PY32F071）**固件不能互刷**（八荒固件 README 有红字警告：刷错直接变砖）；
2. 新代四款（K5 V3 / K6 V3 / K1 / K1(8)）**MCU 与射频芯片都相同**：PY32F071 + BK4829；
   差异在外壳/按键、是否带外部 SPI Flash、以及刷机口（USB-C DFU vs 串口线）；
3. 老代与新代的射频芯片**不是同一颗**：BK4819 vs BK4829（见下节），寄存器地址体系相近但配置值差异很大。

---

## 2. BK4819 与 BK4829 的寄存器差异（源码级对比）

### 2.1 对比方法

- 取**同一仓库**里的两份驱动（老芯片 `bk4819.c`、新芯片 `bk4829.c`），两者**共用同一份寄存器头文件**
  `bk4819-regs.h`（地址以 enum 定义 + 位域用 `#define`）；
- 解析全部 `BK4819_WriteRegister(reg, value)` 调用，用头文件把宏名映射成寄存器地址；
- 统计：写次数、涉及寄存器集合、共同/独有寄存器，以及每个寄存器"出现过的写入值集合"是否相同。

### 2.2 统计结果

| 指标 | 结果 |
|---|---|
| 寄存器地址定义（共用头文件） | **73 个** → 地址 / 编号体系相同 |
| 写寄存器调用次数 | 老 `bk4819` = 144 / 新 `bk4829` = **181** |
| 涉及寄存器个数 | 老 52 / 新 **65** |
| 共同寄存器 | **51 个** = 写入值完全相同 **27 个（53%）** + 写入值不同 **24 个（47%）** |
| 仅老驱动写 | `0x32`（1 个） |
| **仅新驱动写（BK4829 新增编程）** | **14 个**：`0x1C`、`0x1D`、`0x1E`、`0x2A`、`0x2B`、`0x2C`、`0x2F`、`0x40`、`0x4A`、`0x53`、`0x5E`、`0x73`、`0x77`、`0x7E` |

### 2.3 关键寄存器的取值差异（初始化与常用路径）

| 寄存器 | BK4819（老驱动） | BK4829（新驱动） | 说明 |
|---|---|---|---|
| `REG_37` | `0x1D0F`（AGC 路径 `0x1F0F`） | **`0x9D1F`**（AGC `0x9F1F`） | bit15 置位，链路使能位布局变了 |
| `REG_30` | `0xC1FE` | **`0xBFF1`** | 使能位组合不同 |
| `REG_1F` | `0x5454` | **`0xC65A`** | |
| `REG_3E` | `0xA037` | **`0x94C6`** | |
| `REG_7D` | `0xE940` | **`0xE920`** | |
| AGC 表 `REG_10~14`、`REG_49` | 在 `InitAGC()` 设置：`0x007A / 0x027B / 0x037B / 0x03BE`、`REG_49=(0<<14)|(50<<7)|32` 分档 | **改到 `Init()` 里**设 `0x0318 / 0x033A / 0x03DB / 0x03DF / 0x0210`、`REG_49 = 0x2AB2`，`InitAGC()` 再写一遍 | AGC 默认参数整体重标 |
| `REG_50` / `REG_51` / `REG_52` | `0xBB20` / `0x804A` / 五档 `0x028F..0xE28F` | `0xBB18` / `0x8040` + 新增 `0xA033` / 两组值 + `(Tail << 13)` | |
| `REG_70` | `0x00E0` | `0x00C3` | TONE 相关 |
| `REG_46` / `REG_47` | `0xA000` / `0x6040` | 新增 `0x600A` / `0x6042` | VOX / 音频 |
| `REG_28` / `REG_29` | 仅运行时改压扩比 | 新增固定初值 `0x0B40` / `0xAA00` | 压扩（compress/expand） |
| `REG_07` | 无固定初值 | 新增 `0x61CE` | 频率控制相关 |

### 2.4 函数层面的差异

- **新驱动新增**：`BK4819_EnterBypass`、`BK4819_EnterRaw`、`BK4819_PlayToneRaw`、`BK4819_PrepareToPlayTone`、
  `BK4819_SetFrequencyScan`、`BK4819_SetRxAudioGain`；
- **新驱动删除**：`BK4819_EnableFrequencyScan` / `BK4819_DisableFrequencyScan`（被 `SetFrequencyScan` 取代）。

### 2.5 结论

- **"同地址、不同配置"**：寄存器编号体系基本相同（共用一份 regs 头文件），API 命名也能沿用
  （新驱动仍叫 `BK4819_*`），但**共同用到的 51 个寄存器里 47% 的写入值不同**，另有 **14 个寄存器只有新芯片才写**；
- 因此：**`bk4819.c` 与 `bk4829.c` 绝不能互换**；跨代移植等于"重新标定"，
  初始化表、AGC 表、音频/压扩/TONE、PLL 相关寄存器都要按芯片重调；
- 若只做"读状态/开关收发"这类浅功能，可以沿用接口习惯；一旦要动灵敏度、AGC、音频链路，必须回到数据手册。

> 注意：老驱动取自新代仓库中的 fork 遗留副本（与新代同代码风格），可能与"真·老芯片主线固件"
> （如 losehu 仓库）在个别细节上不同；但关键寄存器（`0x30/0x37/0x3E/0x7D` 与 AGC 表）的差异与社区共识一致。

---

## 3. 复现脚本（把两份驱动放在同一目录后运行）

```python
# 用法: python cmp_bk48.py <目录>   （目录下需有 new_bk4819.c / new_bk4829.c / new_regs.h）
import re, sys, collections
d = sys.argv[1]
def load(f): return open(d + '/' + f, encoding='utf-8', errors='ignore').read()
hdr = load('new_regs.h')
regmap = {}
for m in re.finditer(r'\b(BK4819_REG_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)[uUlL]*', hdr):
    v = m.group(2); regmap[m.group(1)] = int(v, 16) if v.lower().startswith('0x') else int(v)
def resolve(tok):
    tok = tok.strip()
    if tok in regmap: return regmap[tok]
    m = re.match(r'^0x([0-9A-Fa-f]+)[uUlL]*$', tok)
    if m: return int(m.group(1), 16)
    m = re.match(r'^(\d+)[uUlL]*$', tok)
    return int(m.group(1)) if m else None
wre = re.compile(r'BK4819_WriteRegister\(\s*([^,]+?)\s*,\s*(.+?)\s*\)\s*;')
def parse(f):
    out = []
    for ln in load(f).splitlines():
        for w in wre.finditer(ln):
            out.append((resolve(w.group(1)), w.group(2).strip()))
    return out
def agg(ws):
    a = collections.defaultdict(collections.Counter)
    for addr, val in ws:
        if addr is not None: a[addr][val] += 1
    return a
o, n = agg(parse('new_bk4819.c')), agg(parse('new_bk4829.c'))
both = sorted(set(o) & set(n))
diff = [r for r in both if o[r] != n[r]]
print(f'共同寄存器 {len(both)}，值不同 {len(diff)}，仅新驱动 {[hex(x) for x in sorted(set(n)-set(o))]}')
for r in diff:
    print(f'  REG_0x{r:02X}: 老{list(o[r])} -> 新{list(n[r])}')
```

---

## 4. 参考来源（均为社区开源工程）

| 内容 | 链接 |
|---|---|
| 新代驱动（K1 / K5 V3，PY32F071，含 `bk4829.c`、共用 `bk4819-regs.h`） | <https://github.com/armel/uv-k1-k5v3-firmware-custom> |
| 老代驱动（K5 / K6，DP32G030） | <https://github.com/losehu/uv-k5-firmware-custom> |
| 八荒固件（UV-K6 V3 / K1 / K1(8)，中文，含机型-主控对照表与变砖警告） | <https://github.com/hyggx/bahuang-firmware> |
| 桌面工具链（硬件支持表、DFU 刷机、实时 LCD 镜像） | <https://github.com/knoinok/quansheng_toolkit> |
| 开源固件奠基（UV-K5 第一份开源固件） | <https://github.com/DualTachyon/uv-k5-firmware> |