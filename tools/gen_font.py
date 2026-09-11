#!/usr/bin/env python3
"""
生成 ST7567 单色点阵 ASCII 字体（0x20..0x7F）。

用法：
  python tools/gen_font.py <out.h>             # 8x16 大字号（标题/强调）
  python tools/gen_font.py --small <out.h>     # 6x8 小字号（列表/正文）

用 Windows 等宽字体栅格化；字体可用 CANDIDATES 更换。
6x8：每字符 8 行、每行 6 像素，存在字节高 6 位（MSB=最左）。
"""

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
            return ImageFont.truetype(p, size=size)
        except Exception:
            continue
    return ImageFont.load_default()


def render(out_path, cw, ch, font_size, baseline, anchor, name, rows_name, comment):
    font = load_font(font_size)
    glyphs = []
    for c in range(0x20, 0x80):
        img = Image.new("1", (cw, ch), 0)
        d = ImageDraw.Draw(img)
        if anchor:
            d.text((0, baseline), chr(c), font=font, fill=1, anchor=anchor)
        else:
            d.text((0, 0), chr(c), font=font, fill=1)
        bits = []
        for y in range(ch):
            b = 0
            for x in range(cw):
                if img.getpixel((x, y)):
                    b |= (0x80 >> x)
            bits.append(b)
        glyphs.append(bits)

    lines = ["#ifndef %s" % name.upper().replace(".", "_").replace("H", "H"),
             "#define %s" % name.upper().replace(".", "_"),
             "",
             "#include <stdint.h>",
             "/* 由 tools/gen_font.py 生成：ASCII 0x20..0x7F, 每字符 %d 行, 每行 %dbit(MSB=左) */" % (ch, cw),
             "const uint8_t %s[96][%d] = {" % (rows_name, ch)]
    for bits in glyphs:
        lines.append("    { " + ", ".join("0x%02X" % b for b in bits) + " },")
    lines += ["};", "", "#endif"]

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d glyphs, %dx%d) %s" % (out_path, len(glyphs), cw, ch, comment))


def main():
    args = sys.argv[1:]
    if args and args[0] == "--small":
        out = args[1] if len(args) > 1 else "font6x8.h"
        render(out, 6, 8, 9, 7, "ls", "font6x8.h", "font6x8", "小字号")
    else:
        out = args[0] if args else "font8x16.h"
        render(out, 8, 16, 16, 0, None, "font8x16.h", "font8x16", "大字号")


if __name__ == "__main__":
    main()