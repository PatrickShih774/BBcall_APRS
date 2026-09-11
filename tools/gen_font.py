#!/usr/bin/env python3
"""
从点阵 BDF 生成 ST7567 用的 ASCII 字模头文件（0x20..0x7F）。

用法：
  python tools/gen_font.py --bdf tools/bdf/7x13.bdf --w 8 --h 16 --out <font8x16.h>
  python tools/gen_font.py --bdf tools/bdf/6x9.bdf  --w 6 --h 8 --baseline 6 ... --out <font6x8.h>

为什么不用 TrueType 栅格化：
  之前用 PIL 把小号 TrueType 渲染成点阵，9px 时笔画常落在半个像素上，阈值一卡就整条竖笔消失
  （实测 Consolas 9px 的 'M' 两条竖线灰度只有 135/141 与 163/121，阈值 128 时右侧那条没了，
  同一批里 H 只剩一竖、K 几乎空白）。小尺寸下栅格化无法稳定，改用**本来就为像素设计的点阵字体**。

字源：X11 misc-fixed 家族，BDF 内声明 `COPYRIGHT "Public domain font. Share and enjoy."`，
属公有领域，可自由分发。仓库内副本在 tools/bdf/，来源与说明见 tools/bdf/README.md。
（参考做法来自 joaquimorg/UV-KX：BDF + bdfconv 转紧凑数组；我们直接自己解析 BDF，不引入 u8g2 依赖。）

BDF 定位规则：
  字形位图左上角所在行 = baseline - BBX.yoff - r（r 为位图第 r 行，0 起）
  列 = BBX.xoff + c
  baseline 取该字体的 FONT_ASCENT，这样 5x7 的大写正好落在行 1..6、降部落在行 7，
  与现有 6x8 单元格的行位完全一致。
"""

import argparse
import sys

BDF_MISSING = []


def parse_bdf(path):
    ascent = descent = None
    copyright_note = ""
    glyphs = {}
    cur = None
    in_bitmap = False
    for raw in open(path, encoding="latin-1"):
        line = raw.rstrip("\n")
        s = line.strip()
        if s.startswith("FONT_ASCENT"):
            ascent = int(s.split()[1])
        elif s.startswith("FONT_DESCENT"):
            descent = int(s.split()[1])
        elif s.startswith("COPYRIGHT"):
            copyright_note = s.split(None, 1)[1].strip('"')
        elif s.startswith("STARTCHAR"):
            cur = {"enc": None, "w": 0, "h": 0, "xo": 0, "yo": 0, "dw": 0, "rows": []}
            in_bitmap = False
        elif cur is not None and s.startswith("ENCODING"):
            cur["enc"] = int(s.split()[1])
        elif cur is not None and s.startswith("DWIDTH"):
            cur["dw"] = int(s.split()[1])
        elif cur is not None and s.startswith("BBX"):
            _, w, h, xo, yo = s.split()
            cur.update(w=int(w), h=int(h), xo=int(xo), yo=int(yo))
        elif cur is not None and s.startswith("BITMAP"):
            in_bitmap = True
        elif cur is not None and s.startswith("ENDCHAR"):
            if in_bitmap and cur["enc"] is not None:
                glyphs[cur["enc"]] = cur
            cur = None
            in_bitmap = False
        elif cur is not None and in_bitmap:
            cur["rows"].append(s)
    return ascent, descent, copyright_note, glyphs


def place(g, cw, ch, baseline, force_bottom=False):
    """把 BDF 字形放进 cw x ch 单元格，返回每行的字节（MSB=最左）"""
    grid = [0] * ch
    nbytes = (g["w"] + 7) // 8
    # BDF 的 BBX 给出位图**左下角**相对基准线的偏移：
    #   位图顶行 = baseline - (yoff + height - 1)
    # （早先写成 baseline - yoff - r，等于把字形上下翻转了，L 的底横会跑到顶上）
    top = baseline - (g["yo"] + g["h"] - 1)
    if force_bottom:
        top = ch - g["h"]
    for r, hexrow in enumerate(g["rows"]):
        if not hexrow:
            continue
        val = int(hexrow, 16)
        y = top + r
        if y < 0 or y >= ch:
            continue
        for b in range(g["w"]):
            if val & (1 << (nbytes * 8 - 1 - b)):
                x = g["xo"] + b
                if 0 <= x < cw:
                    grid[y] |= (0x80 >> x)
    return grid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bdf", required=True)
    ap.add_argument("--w", type=int, required=True)
    ap.add_argument("--h", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", required=True, help='C 数组名，如 font6x8')
    ap.add_argument("--macro", required=True, help='头文件宏名，如 FONT6X8_H')
    ap.add_argument("--baseline", type=int, default=None, help="默认取 FONT_ASCENT")
    ap.add_argument("--comment", default="")
    a = ap.parse_args()

    ascent, descent, note, glyphs = parse_bdf(a.bdf)
    if ascent is None:
        print("BDF 缺少 FONT_ASCENT", file=sys.stderr)
        return 2
    baseline = a.baseline if a.baseline is not None else ascent

    rows = []
    missing = []
    blank = []
    for code in range(0x20, 0x80):
        g = glyphs.get(code)
        if g is None or g["w"] == 0:
            rows.append([0] * a.h)
            if code != 0x20:
                missing.append(chr(code))
            continue
        grid = place(g, a.w, a.h, baseline)
        if code != 0x20 and not any(grid):
            # 兜底：整字落在单元格之外（例：下划线的墨迹正好在降部被裁掉）时改为贴底放置，
            # 保证有字形而不是画成空白。
            grid = place(g, a.w, a.h, baseline, force_bottom=True)
        rows.append(grid)
        if code != 0x20 and not any(grid):
            blank.append(chr(code))

    out = [
        "#ifndef %s" % a.macro,
        "#define %s" % a.macro,
        "",
        "#include <stdint.h>",
        "/* 由 tools/gen_font.py 从 BDF 生成：ASCII 0x20..0x7F, 每字符 %d 行, 每行 %dbit(MSB=左) */" % (a.h, a.w),
        "/* 字源：%s（%s） */" % (a.bdf.replace("\\", "/"), note or "见 BDF"),
        "const uint8_t %s[96][%d] = {" % (a.name, a.h),
    ]
    for bits in rows:
        out.append("    { " + ", ".join("0x%02X" % b for b in bits) + " },")
    out += ["};", "", "#endif"]
    open(a.out, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")

    print("wrote %s" % a.out)
    print("  字源 %s  单元格 %dx%d  baseline=%d (ASCENT=%d DESCENT=%s)  %s"
          % (a.bdf, a.w, a.h, baseline, ascent, descent, a.comment))
    print("  版权: %s" % (note or "(BDF 内无声明)"))
    if missing:
        print("  缺失字形: %s" % " ".join(missing))
    if blank:
        print("  空白字形（非空格却无点）: %s" % " ".join(blank))
    if not missing and not blank:
        print("  覆盖检查: 0x20..0x7F 全部有字形且非空")
    return 0


if __name__ == "__main__":
    sys.exit(main())