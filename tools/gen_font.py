#!/usr/bin/env python3
"""
生成 8x16 单色点阵 ASCII 字体（0x20..0x7F），供 ST7567 显示。
输出: firmware/Src/font8x16.h 的 C 数组。
用 Windows 等宽字体栅格化；字体可用 gen_font.py 的路径参数更换。
"""

import sys
from PIL import Image, ImageDraw, ImageFont


CANDIDATES = [
    r"C:\Windows\Fonts\consola.ttf",   # Consolas (等宽)
    r"C:\Windows\Fonts\cour.ttf",      # Courier New
    r"C:\Windows\Fonts\lucon.ttf",     # Lucida Console
    r"C:\Windows\Fonts\arial.ttf",
]


def load_font(size: int = 16):
    for p in CANDIDATES:
        try:
            return ImageFont.truetype(p, size=size)
        except Exception:
            continue
    return ImageFont.load_default()


def main(out_path: str):
    font = load_font(16)
    rows = []
    for c in range(0x20, 0x80):
        img = Image.new("1", (8, 16), 0)
        d = ImageDraw.Draw(img)
        # 垂直居中、留一点边距
        d.text((0, 0), chr(c), font=font, fill=1)
        # 转成每行一个字节（MSB=最左）
        bits = []
        for y in range(16):
            b = 0
            for x in range(8):
                if img.getpixel((x, y)):
                    b |= (0x80 >> x)
            bits.append(b)
        rows.append(bits)

    lines = []
    lines.append("#ifndef FONT8X16_H")
    lines.append("#define FONT8X16_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("/* 由 tools/gen_font.py 生成：ASCII 0x20..0x7F, 每字符 16 行, 每行 8bit(MSB=左) */")
    lines.append("const uint8_t font8x16[96][16] = {")
    for bits in rows:
        s = ", ".join("0x%02X" % b for b in bits)
        lines.append("    { " + s + " },")
    lines.append("};")
    lines.append("")
    lines.append("#endif")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d glyphs)" % (out_path, len(rows)))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "firmware/Src/font8x16.h"
    main(out)
