#!/usr/bin/env python3
"""
生成 ST7567 单色点阵 ASCII 字体（0x20..0x7F）。

用法：
  python tools/gen_font.py <out.h>               # 8x16 大字号（标题）
  python tools/gen_font.py --small <out.h>       # 6x8 小字号（列表/正文/菜单）

为什么用「灰度渲染 + 阈值」而不是 PIL 的 mode "1"：
  9px 的 TrueType 字形笔画常常落在半个像素上，mode "1" 的内部阈值会把其中一条竖笔
  整条吃掉。实测 Consolas 9px 的 'M' 两条竖线灰度只有 135/141 与 163/121，
  阈值 128 时右侧那条消失、H 只剩一竖、K 几乎空白。改成灰度渲染后自己按阈值二值化，
  并跑一遍「脆弱性检查」（阈值上下浮动后字形是否剧变），就能在生成阶段发现这类问题。

字源许可：脚本读取本机字体，只输出点阵数据；仓库里分发字模前请确认字源许可。
本项目 ASCII 字模由本脚本从系统等宽字体生成。
"""

import argparse
import sys
from PIL import Image, ImageDraw, ImageFont

CANDIDATES = [
    r"C:\Windows\Fonts\consola.ttf",   # Consolas (等宽)
    r"C:\Windows\Fonts\cour.ttf",      # Courier New
    r"C:\Windows\Fonts\lucon.ttf",     # Lucida Console
    r"C:\Windows\Fonts\arial.ttf",
]


def load_font(size):
    for p in CANDIDATES:
        try:
            return p, ImageFont.truetype(p, size=size)
        except Exception:
            continue
    return None, ImageFont.load_default()


def render_glyph(font, ch, cw, ch_h, baseline, anchor, thr):
    img = Image.new("L", (cw, ch_h), 0)
    d = ImageDraw.Draw(img)
    if anchor:
        d.text((0, baseline), ch, font=font, fill=255, anchor=anchor)
    else:
        d.text((0, 0), ch, font=font, fill=255)
    return [sum((0x80 >> x) for x in range(cw) if img.getpixel((x, y)) >= thr)
            for y in range(ch_h)]


def fragility(font, cw, ch_h, baseline, anchor, thr, span=24, limit=6):
    """阈值 ±span 后字形变化超过 limit 位的字符（越少越好）"""
    bad = []
    for c in range(0x20, 0x80):
        a = render_glyph(font, chr(c), cw, ch_h, baseline, anchor, thr - span)
        b = render_glyph(font, chr(c), cw, ch_h, baseline, anchor, thr + span)
        d = sum(bin(a[y] ^ b[y]).count("1") for y in range(ch_h))
        if d > limit:
            bad.append((chr(c), d))
    return bad


def emit(out_path, cw, ch_h, font_size, baseline, anchor, name, rows_name, comment, thr):
    path, font = load_font(font_size)
    rows = [render_glyph(font, chr(c), cw, ch_h, baseline, anchor, thr)
            for c in range(0x20, 0x80)]

    lines = [
        "#ifndef %s" % name.upper().replace(".", "_"),
        "#define %s" % name.upper().replace(".", "_"),
        "",
        "#include <stdint.h>",
        "/* 由 tools/gen_font.py 生成：ASCII 0x20..0x7F, 每字符 %d 行, 每行 %dbit(MSB=左) */" % (ch_h, cw),
        "/* 字源：%s size=%d，灰度渲染后阈值 %d 二值化 */" % (path or "PIL default", font_size, thr),
        "const uint8_t %s[96][%d] = {" % (rows_name, ch_h),
    ]
    for bits in rows:
        lines.append("    { " + ", ".join("0x%02X" % b for b in bits) + " },")
    lines += ["};", "", "#endif"]

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    bad = fragility(font, cw, ch_h, baseline, anchor, thr)
    print("wrote %s (%d glyphs, %dx%d, %s, thr=%d)" % (out_path, len(rows), cw, ch_h, comment, thr))
    print("  字源: %s size=%d" % (path or "PIL default", font_size))
    if bad:
        print("  脆弱字符（阈值 ±24 变化 >6 位，说明笔画压在半像素上）: %s"
              % " ".join("%s%d" % (c, d) for c, d in bad))
    else:
        print("  脆弱字符: 无")
    return len(bad)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="font8x16.h")
    ap.add_argument("--small", action="store_true")
    args = ap.parse_args()
    if args.small:
        emit(args.out, 6, 8, 9, 7, "ls", "font6x8.h", "font6x8", "小字号", 96)
    else:
        emit(args.out, 8, 16, 16, 0, None, "font8x16.h", "font8x16", "大字号", 128)


if __name__ == "__main__":
    sys.exit(0 if main() is None else 0)