#!/usr/bin/env python3
"""
生成中文字库子集（16x16 点阵），布局沿用 Dondji 的形状。

参考：EthanYan6/Dondji（Apache-2.0）在 128x64 单色 LCD 上的中文字库方案。
它把字库放在**外部 SPI Flash**，固件里只留布局常量；本脚本沿用同一形状，
但本项目（STM32F103C8T6，64KB Flash）暂时只做**片上子集**：

    [0]                        位图区   N x 32 字节（16 行 x uint16_t，MSB=最左）
    [BITMAP_SIZE]              索引表   N x 4 字节（Unicode 码点升序 -> 第 i 项即位图第 i 个）
    [BITMAP_SIZE+INDEX_SIZE]   版本字节 1 字节

保留这个形状是为了以后真加了外部 SPI Flash 时，读取逻辑一行不用改，
只需在索引表与版本字节之间插入拼音表（Dondji 就是这么做的）。

用法：
  python tools/gen_cn_font.py --unifont <unifont.hex> --chars "收件箱消息" \
      --out-header firmware-stm32porject/Core/Inc/cn_font_data.h \
      --out-bin tools/cn_font.bin --budget 38000

字源许可提醒：不要用 WenQuanYi Bitmap Song（GPL v2 only + 字体嵌入例外），
它与本项目的 GPL-3.0 不兼容。默认推荐 GNU Unifont（OFL-1.1 或 GPLv2+ 双许可）。
"""

import argparse
import os
import sys

GLYPH_W = 16
GLYPH_H = 16
GLYPH_BYTES = GLYPH_H * 2          # 每行一个 uint16_t
INDEX_ENTRY = 4                    # Unicode 码点，升序
VERSION = 1


def load_unifont(path):
    """.hex 格式：`码点:16 进制点阵`，16x16 为 64 个 hex 字符。"""
    glyphs = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or ":" not in line:
                continue
            cp, data = line.split(":", 1)
            if len(data) != GLYPH_W * GLYPH_H // 4:
                continue                # 跳过 8x16 半宽字形
            try:
                glyphs[int(cp, 16)] = bytes.fromhex(data)
            except ValueError:
                continue
    return glyphs


def load_bdf(path):
    """BDF：只取 16x16（BBX 16 16 或可裁剪到 16x16）的字形。"""
    glyphs = {}
    cur_cp = None
    rows = []
    in_bitmap = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("ENCODING"):
                cur_cp = int(line.split()[1])
            elif line.startswith("BBX"):
                _, w, h, _, _ = line.split()
                if (int(w), int(h)) != (GLYPH_W, GLYPH_H):
                    cur_cp = None
            elif line.startswith("BITMAP"):
                in_bitmap = True
                rows = []
            elif line.startswith("ENDCHAR"):
                in_bitmap = False
                if cur_cp is not None and len(rows) == GLYPH_H:
                    glyphs[cur_cp] = bytes.fromhex("".join(rows))
                cur_cp = None
            elif in_bitmap:
                rows.append(line.strip())
    return glyphs


def build(chars, glyphs):
    """返回 (位图 bytes, 索引 bytes, 缺失字符列表)"""
    codes = sorted({ord(c) for c in chars if ord(c) > 0x7F})
    bitmap = bytearray()
    index = bytearray()
    missing = []
    kept = []
    for cp in codes:
        g = glyphs.get(cp)
        if g is None:
            missing.append(chr(cp))
            continue
        bitmap += g
        index += cp.to_bytes(INDEX_ENTRY, "little")
        kept.append(cp)
    return bytes(bitmap), bytes(index), missing, kept


def write_header(path, bitmap, index, chars_kept):
    n = len(index) // INDEX_ENTRY
    bm_size = len(bitmap)
    idx_size = len(index)
    ver_off = bm_size + idx_size
    lines = []
    lines.append("/*")
    lines.append(" * 中文字库子集（自动生成，请勿手改）")
    lines.append(" *")
    lines.append(" * 生成工具：tools/gen_cn_font.py")
    lines.append(" * 字源：GNU Unifont（OFL-1.1 或 GPLv2+ 双许可），见 THIRD_PARTY_NOTICES.md")
    lines.append(" * 布局参考：EthanYan6/Dondji（Apache-2.0）的外部 SPI Flash 字库形状")
    lines.append(" *")
    lines.append(" * 布局：位图(%d B) + Unicode 索引(%d B, 4B/项, 升序) + 版本字节(1 B) = %d B"
                 % (bm_size, idx_size, ver_off + 1))
    lines.append(" */")
    lines.append("#ifndef CN_FONT_DATA_H")
    lines.append("#define CN_FONT_DATA_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define CN_FONT_CHAR_COUNT     %du" % n)
    lines.append("#define CN_FONT_BITMAP_SIZE    %du" % bm_size)
    lines.append("#define CN_FONT_INDEX_SIZE     %du" % idx_size)
    lines.append("#define CN_FONT_VERSION         %du" % VERSION)
    lines.append("#define CN_FONT_VERSION_OFFSET %du" % ver_off)
    lines.append("#define CN_FONT_GLYPH_BYTES    %du" % GLYPH_BYTES)
    lines.append("")
    lines.append("/* 位图：%d 字 x %d 行 x uint16_t（MSB = 最左像素） */" % (n, GLYPH_H))
    lines.append("static const uint16_t cn_font_bitmaps[%d] = {" % (n * GLYPH_H))
    for i in range(n):
        row = []
        for r in range(GLYPH_H):
            off = i * GLYPH_BYTES + r * 2
            row.append("0x%02X%02X" % (bitmap[off], bitmap[off + 1]))
        code = int.from_bytes(index[i * 4:(i + 1) * 4], "little")
        lines.append("    /* [%3d] U+%04X %s */  %s," %
                     (i, code, chr(code), ", ".join(row)))
    lines.append("};")
    lines.append("")
    lines.append("/* Unicode 索引，升序；第 i 项的位图偏移 = i * CN_FONT_GLYPH_BYTES */")
    lines.append("static const uint32_t cn_font_index[%d] = {" % n)
    for i in range(0, n, 8):
        chunk = ["0x%04Xu" % int.from_bytes(index[(i + k) * 4:(i + k + 1) * 4], "little")
                 for k in range(min(8, n - i))]
        lines.append("    " + ", ".join(chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* CN_FONT_DATA_H */")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unifont", help="unifont .hex 路径")
    ap.add_argument("--bdf", help="BDF 字体路径（需 16x16 字形）")
    ap.add_argument("--chars", default="", help="要收录的字符（直接给字符串）")
    ap.add_argument("--chars-file", help="要收录的字符（每行一串，忽略 # 开头）")
    ap.add_argument("--out-header", default="firmware-stm32porject/Core/Inc/cn_font_data.h")
    ap.add_argument("--out-bin", default="tools/cn_font.bin")
    ap.add_argument("--budget", type=int, default=38000, help="片上 Flash 预算（字节）")
    args = ap.parse_args()

    if not args.unifont and not args.bdf:
        print("需要 --unifont 或 --bdf", file=sys.stderr)
        return 2
    glyphs = load_unifont(args.unifont) if args.unifont else load_bdf(args.bdf)
    if not glyphs:
        print("字源里没有 16x16 字形", file=sys.stderr)
        return 3

    chars = args.chars
    if args.chars_file:
        with open(args.chars_file, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    chars += line

    bitmap, index, missing, kept = build(chars, glyphs)
    n = len(kept)
    total = len(bitmap) + len(index) + 1
    write_header(args.out_header, bitmap, index, kept)
    with open(args.out_bin, "wb") as f:
        f.write(bitmap + index + bytes([VERSION]))

    print("字源字形数 : %d" % len(glyphs))
    print("收录字符数 : %d（缺失 %d）" % (n, len(missing)))
    if missing:
        print("缺失字符   : %s" % "".join(missing))
    print("位图       : %d B" % len(bitmap))
    print("索引       : %d B" % len(index))
    print("合计       : %d B（占预算 %.1f%%）" % (total, 100.0 * total / args.budget))
    print("可容纳上限 : 约 %d 字（按当前预算）" % ((args.budget - 1) // (GLYPH_BYTES + INDEX_ENTRY)))
    print("输出       : %s , %s" % (args.out_header, args.out_bin))
    return 0


if __name__ == "__main__":
    sys.exit(main())