# tools/bdf — ASCII 字模的 BDF 源

`tools/gen_font.py` 从这里读点阵 BDF，生成固件用的字模头文件。

| 文件 | 用途 | 单元格 | FONT_ASCENT | FONT_DESCENT | 生成参数 |
|---|---|---|---|---|---|
| `6x9.bdf` | 小字号（列表 / 正文 / 状态栏） | 6x8 | 7 | 2 | `--w 6 --h 8 --baseline 6` |
| `7x13.bdf` | 大字号（标题） | 8x16 | 11 | 2 | `--w 8 --h 16` |

## 来源与许可

两者都来自 **X11 misc-fixed** 字族，BDF 内自带声明：

```
COPYRIGHT "Public domain font.  Share and enjoy."
```

即**公有领域**，可自由使用与再分发，与本项目的 GPL-3.0 无冲突。
仓库内副本取自 [olikraus/u8g2](https://github.com/olikraus/u8g2) 的 `tools/font/bdf/`
（u8g2 库本身是 BSD-2；字体许可以各 BDF 内的声明为准，这两个是公有领域）。

同目录曾经比较过 5x7 / 5x8 / 6x10：5x7 的 M/W 只有 4 列宽且形状偏弱，5x8 与 6x10 行高超出
我们的 8 行单元格，故未采用；如需重新评估可从 u8g2 仓库再取。

## 为什么不用 TrueType 栅格化

小尺寸的 TrueType 笔画常落在半个像素上，阈值一卡就整条竖笔消失。实测 Consolas 9px 的 `M`
两条竖线灰度只有 135/141 与 163/**121**，阈值 128 时右侧那条被吃掉，同一批里 `H` 只剩一竖、
`K` 几乎空白。**小字号必须用本来为像素设计的点阵字**，这也是 joaquimorg/UV-KX 的做法
（它用 BDF + u8g2 的 bdfconv；我们直接自己解析 BDF，不引入 u8g2 依赖）。

## 重新生成

```bash
python tools/gen_font.py --bdf tools/bdf/6x9.bdf  --w 6 --h 8  --baseline 6 \
    --name font6x8  --macro FONT6X8_H  --out firmware-stm32porject/Core/Inc/font6x8.h
python tools/gen_font.py --bdf tools/bdf/7x13.bdf --w 8 --h 16 \
    --name font8x16 --macro FONT8X16_H --out firmware-stm32porject/Core/Inc/font8x16.h
```

生成器会报告：字源、单元格、基线、版权声明，以及 0x20..0x7F 的**缺字与空白字形**检查。

## 换字体时的注意事项

- 基线取自 BDF 的 `FONT_ASCENT`（可用 `--baseline` 覆盖）。小字号取 6 是为了让大写正好落在
  行 1..6，与既有版面约定一致；`6x9` 的降部有 2 行而单元格只留 1 行，所以**降部会裁掉 1 行**
  （只影响 `g j p q y` 的尾巴）。
- 若整字落在单元格之外，生成器会退化为**贴底放置**（`_` 就属于这种情况），保证不会画出空白。
- 改完务必抽查 `M W H K N L` 与降部 `g j p q y`。