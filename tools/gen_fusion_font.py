#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fusion_font.py -- 从原型 HTML 内嵌字模生成 C 字模头文件

字模来源：bbcall-aprs-screen-states.html 内嵌的 G12 / G10 两张字形表，
其上游是 Fusion Pixel Font（TakWolf, SIL OFL 1.1）12px / 10px 单宽版 BDF，
提取范围 = 原型实际用到的 374 字形（95 ASCII + 279 汉字），与 design.md §3.5 一致。

字形表条目格式：  "ox,oy,gw,hex"
  ox,oy = 字形在单元格内的偏移；gw = 字形点阵宽度（像素）
  hex   = 逐行位图，每行 ceil(gw/4) 个十六进制字符，行数 = len(hex)/hp；
          每行第 b 位（gw-1-b 为置位测试位）为 1 表示 (ox+b, oy+r) 点亮

输出：firmware-stm32porject/Core/Inc/fusion_font.h（固件与模拟器共用这一份）
  每个字形存 ox/oy/gw/rows + 行位图（每行 ceil(gw/8) 字节，高位在左）。
  渲染逻辑与原型 glyph() 逐位一致；ASCII 直接按下标 cp-0x20 查，
  汉字按码位升序表二分查找。改动提取范围后重跑本脚本即可。
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
HTML = ROOT / "bbcall-aprs-screen-states.html"
OUT = ROOT / "firmware-stm32porject" / "Core" / "Inc" / "fusion_font.h"


def parse_table(src, name):
    m = re.search(r"var %s = \{(.*?)\};" % name, src, re.S)
    if not m:
        raise SystemExit("在 HTML 里找不到 %s 字模表" % name)
    table = {}
    for km, vm in re.findall(r'(\d+):"([^"]*)"', m.group(1)):
        table[int(km)] = vm
    return table


def pack_glyph(spec):
    """返回 (ox, oy, gw, rows, rowbytes list)。与原型逐位等价。"""
    p = spec.split(",")
    ox, oy, gw = int(p[0]), int(p[1]), int(p[2])
    hexs = p[3] if len(p) > 3 else ""
    if gw == 0 or not hexs:
        return ox, oy, gw, 0, []
    hp = (gw + 3) // 4
    rows = len(hexs) // hp
    rowbytes = []
    for r in range(rows):
        v = int(hexs[r * hp:(r + 1) * hp], 16)
        nbytes = (gw + 7) // 8
        rowbytes.append(v.to_bytes(nbytes, "big"))
    return ox, oy, gw, rows, rowbytes


def emit_glyph_lines(var, table, keys):
    lines = []
    meta = []
    for cp in keys:
        ox, oy, gw, rows, rb = pack_glyph(table[cp])
        meta.append((ox, oy, gw, rows, len(rb[0]) if rb else 0))
        lines.append((cp, rb))
    return meta, lines


def main():
    src = HTML.read_text(encoding="utf-8")
    g12 = parse_table(src, "G12")
    g10 = parse_table(src, "G10")
    ascii_keys = [cp for cp in sorted(g12) if cp < 0x2E80]
    cjk_keys = [cp for cp in sorted(g12) if cp >= 0x2E80]
    if ascii_keys != list(range(0x20, 0x20 + 95)):
        raise SystemExit("ASCII 覆盖不是连续的 0x20..0x7E，请检查字模表")
    assert set(g10) == set(g12), "G10 与 G12 字形覆盖不一致"

    out = []
    out.append("/* 本文件由 tools/gen_fusion_font.py 自动生成，勿手改。")
    out.append(" * 字模来源：Fusion Pixel Font（TakWolf, SIL OFL 1.1）12px/10px 单宽版，")
    out.append(" * 经 bbcall-aprs-screen-states.html 内嵌字形表提取（374 字形：95 ASCII + 279 汉字）。")
    out.append(" * 行位图每行 ceil(gw/8) 字节，高位在左；渲染见 ui_harness.c 的 fp_glyph()。")
    out.append(" */")
    out.append("#ifndef FUSION_FONT_H")
    out.append("#define FUSION_FONT_H")
    out.append("#include <stdint.h>")
    out.append("")

    for tier, table, cell_w, cell_h in (("12", g12, 6, 12), ("10", g10, 5, 10)):
        ascii_meta, ascii_rows = emit_glyph_lines("FP_A" + tier, table, ascii_keys)
        out.append("#define FP%s_CELL_A %du   /* ASCII 推进宽度（像素） */" % (tier, cell_w))
        out.append("#define FP%s_CELL_W %du   /* 汉字推进宽度（像素） */" % (tier, cell_w * 2))
        out.append("#define FP%s_CELL_H %du   /* 行高（像素） */" % (tier, cell_h))
        out.append("")
        if not any("fp_glyph_meta_t;" in l for l in out):
            out.append("typedef struct { uint8_t ox, oy, gw, rows, rb; uint16_t off; } fp_glyph_meta_t;")
            out.append("")
        # 位图数据：ASCII 95 字形顺序排列，每字形 rows*rb 字节，用元数据表定位
        bitmap = []
        offs = []
        pos = 0
        for cp, rb in ascii_rows:
            offs.append(pos)
            for b in rb:
                bitmap.extend(b)
            pos += sum(len(x) for x in rb)
        cjk_meta, cjk_rows = emit_glyph_lines("FP_C" + tier, table, cjk_keys)
        cjk_offs = []
        for cp, rb in cjk_rows:
            cjk_offs.append(pos)
            for b in rb:
                bitmap.extend(b)
            pos += sum(len(x) for x in rb)
        out.append("static const uint8_t fp%s_bitmap[] = {" % tier)
        for i in range(0, len(bitmap), 16):
            out.append("  " + ",".join(str(b) for b in bitmap[i:i + 16]) + ",")
        out.append("};")
        out.append("")
        out.append("static const fp_glyph_meta_t fp%s_ascii_meta[95] = {" % tier)
        for (ox, oy, gw, rows, rbn), off in zip(ascii_meta, offs):
            out.append("  {%d,%d,%d,%d,%d,%d}," % (ox, oy, gw, rows, rbn, off))
        out.append("};")
        out.append("#define FP%s_CJK_N %d" % (tier, len(cjk_keys)))
        out.append("static const uint16_t fp%s_cjk_code[] = {" % tier)
        for i in range(0, len(cjk_keys), 12):
            out.append("  " + ",".join(str(c) for c in cjk_keys[i:i + 12]) + ",")
        out.append("};")
        out.append("static const fp_glyph_meta_t fp%s_cjk_meta[FP%s_CJK_N] = {" % (tier, tier))
        for (ox, oy, gw, rows, rbn), off in zip(cjk_meta, cjk_offs):
            out.append("  {%d,%d,%d,%d,%d,%d}," % (ox, oy, gw, rows, rbn, off))
        out.append("};")
        out.append("")

    out.append("#endif /* FUSION_FONT_H */")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("写出 %s（ASCII 95 × 2 档，汉字 %d × 2 档）" % (OUT, len(cjk_keys)))


if __name__ == "__main__":
    main()
