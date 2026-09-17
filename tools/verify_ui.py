#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_ui.py -- design.md v2.0 三态界面逐像素读屏校验（design.md §13 的可执行版）

方法：从 docs/bbcall-aprs-screen-states.html 内嵌的 G12/G10 字模（与固件 fusion_font.h
同一份数据）按字符格做模板匹配：正显 / 反显两种解释取总距离更小者，
强制极性的区域（反显底挖字）直接指定。任一像素不匹配即 FAIL。

固定输入约定（与自检命令一致）：
  bbcall_sim.exe --selftest --clock 51960 --wallclock 3,9,16 \
                 --mycall BG5BLH --batt 2 --screen <态> --out x.bmp
  --demo 内置 4 条（3 中文消息 + 1 位置帧），最新在队首：
    BG5BLB :有内鬼 停止交易  RSSI -92 SNR 12
    BG5BLH :中继信号 正常    RSSI -88 SNR 18
    N0CALL :位置已存 电量低   RSSI -95 SNR 9
    BG5AOZ =2954.05N/12132.86E>...（位置帧）

用法：
  python tools/verify_ui.py simulator/build-win/v3_idle.bmp idle
  python tools/verify_ui.py FILE           # 列出可用的屏幕规格
"""
import re
import sys
import struct

ROOT = __file__.rsplit("\\tools\\", 1)[0] if "\\tools\\" in __file__ else __file__.rsplit("/tools/", 1)[0]
HTML = ROOT + "/docs/bbcall-aprs-screen-states.html"

# ---------------------------------------------------------------- 字模解析
# G12/G10 条目："ox,oy,gw,hex"（每行 ceil(gw/4) 个十六进制字符，行数=len/hp；
# 行内像素 b 置位测试：v & (1 << (gw-1-b))，与固件 fp_glyph_draw 一致）

def parse_fonts():
    src = open(HTML, encoding="utf-8").read()
    out = {}
    for name in ("G12", "G10"):
        m = re.search(r"var %s = \{(.*?)\};" % name, src, re.S)
        table = {}
        for km, vm in re.findall(r'(\d+):"([^"]*)"', m.group(1)):
            p = vm.split(",")
            ox, oy, gw = int(p[0]), int(p[1]), int(p[2])
            hexs = p[3] if len(p) > 3 else ""
            rows = []
            if gw and hexs:
                hp = (gw + 3) // 4
                for r in range(len(hexs) // hp):
                    v = int(hexs[r * hp:(r + 1) * hp], 16)
                    rows.append([(v >> (gw - 1 - b)) & 1 for b in range(gw)])
            table[int(km)] = (ox, oy, gw, rows)
        out[int(name[1:])] = table
    return out

FONTS = parse_fonts()
ADV = {12: {"n": 6, "w": 12}, 10: {"n": 5, "w": 10}}

def is_wide(cp):
    return cp >= 0x2E80

def text_cells(s, tier):
    t = 0
    for ch in s:
        cp = ord(ch)
        t += ADV[tier]["w" if is_wide(cp) else "n"]
    return t

def cx(s, tier, x0, x1, k=1):
    return x0 + (x1 - x0 + 1 - text_cells(s, tier) * k) // 2

# ---------------------------------------------------------------- BMP 读取

def read_bmp(path):
    """返回 (pixels, scale)：pixels 为 128x64 的 0/1 点阵（1=墨）"""
    raw = open(path, "rb").read()
    off = struct.unpack_from("<I", raw, 10)[0]
    w = struct.unpack_from("<i", raw, 18)[0]
    h = struct.unpack_from("<i", raw, 22)[0]
    bpp = struct.unpack_from("<H", raw, 28)[0]
    if bpp not in (24, 32):
        raise SystemExit(f"unsupported bpp {bpp}")
    topdown = h < 0
    h = abs(h)
    stride = (w * bpp // 8 + 3) & ~3
    px = [[0] * w for _ in range(h)]
    for y in range(h):
        row = (y if topdown else h - 1 - y)
        base = off + row * stride
        for x in range(w):
            b, g, r = raw[base + x * (bpp // 8): base + x * (bpp // 8) + 3]
            px[y][x] = 1 if (r + g + b) < 384 else 0
    scale = w // 128
    if scale < 1 or w % 128 != 0 or h != 64 * scale:
        raise SystemExit(f"unexpected BMP size {w}x{h}")
    out = [[0] * 128 for _ in range(64)]
    for y in range(64):
        for x in range(128):
            blk = [px[yy][xx]
                   for yy in range(y * scale, (y + 1) * scale)
                   for xx in range(x * scale, (x + 1) * scale)]
            out[y][x] = 1 if sum(blk) > len(blk) // 2 else 0
    return out, scale

# ---------------------------------------------------------------- 匹配

def match_text(px, x, y, text, tier, k=1):
    """在 (x,y) 处匹配 text（Fusion 字模）；返回 (bad_pixels, polarity)。
    polarity: 0=正显(墨字) 1=反显(挖字)"""
    bad = [0, 0]
    cur = x
    for ch in text:
        cp = ord(ch)
        g = FONTS[tier].get(cp)
        if g and g[2]:
            ox, oy, gw, rows = g
            for r, rowbits in enumerate(rows):
                for b, want in enumerate(rowbits):
                    for sy in range(k):
                        for sx in range(k):
                            yy = y + (oy + r) * k + sy
                            xx = cur + (ox + b) * k + sx
                            if yy >= 64 or xx >= 128 or yy < 0 or xx < 0:
                                continue
                            got_on = px[yy][xx] == 1
                            if (want == 1) != got_on:
                                bad[0] += 1
                            if (want == 1) == got_on:
                                bad[1] += 1
        cur += ADV[tier]["w" if is_wide(cp) else "n"] * k
    return bad

def check(px, x, y, text, tier=12, k=1, inv=None):
    """inv=None 时自动按正/反显取更优；指定 0/1 时强制该极性"""
    bad_n, bad_i = match_text(px, x, y, text, tier, k)
    pol, bad = (0, bad_n) if bad_n <= bad_i else (1, bad_i)
    if inv is not None:
        pol, bad = inv, (bad_n if inv == 0 else bad_i)
    return bad, pol

def check_vline(px, x, y0, y1):
    return sum(1 for y in range(y0, y1 + 1) if px[y][x] != 1)

def check_hline(px, y, x0, x1):
    return sum(1 for x in range(x0, x1 + 1) if px[y][x] != 1)

def check_rect_fill(px, x0, y0, x1, y1, want=1):
    """矩形填充检查（反显底用）：四角 + 全边框抽点"""
    bad = 0
    for y in range(y0, y1 + 1):
        for x in (x0, x1):
            if px[y][x] != want:
                bad += 1
    for x in range(x0, x1 + 1):
        for y in (y0, y1):
            if px[y][x] != want:
                bad += 1
    return bad

# ---------------------------------------------------------------- 屏幕规格
# 固定输入：--clock 51960 --wallclock 3,9,16 --mycall BG5BLH --batt 2 --demo
# demo 入箱后队首（最新）= BG5BLB「有内鬼 停止交易」RSSI -92 SNR 12，
# 无路径（npath=0 → path "-"），CRC=OK；未读 4。

def R(x, y, s, tier=12, k=1, inv=None):
    return ("text", x, y, s, tier, k, inv)

def RC(x0, x1, y, s, tier=12, k=1, inv=None):
    return ("text_cx", x0, x1, y, s, tier, k, inv)

SPEC = {
    "idle": [
        ("fill", 0, 0, 63, 63, 1),                       # 大格恒反显
        RC(0, 63, 11, "14:26", 12, 2, 1),                # 时钟 2 倍挖字（冒号亮档：51960000/500 为偶）
        RC(0, 63, 41, "周三 9/16", 12, 1, 1),
        RC(65, 127, 2, "本机", 10, 1, 0),
        RC(65, 127, 18, "BG5BLH", 12, 1, 0),
        RC(65, 95, 34, "电量", 10, 1, 0),
        RC(65, 95, 50, "高", 12, 1, 0),
        RC(97, 127, 34, "未读", 10, 1, 0),
        RC(97, 127, 50, "4", 12, 1, 0),
        ("vline", 64, 0, 63), ("hline", 32, 64, 127), ("vline", 96, 32, 63),
    ],
    "unread": [
        ("fill", 0, 0, 63, 63, 1),
        RC(0, 63, 2, "BG5BLB", 12, 1, 1),
        RC(0, 63, 18, "有内鬼", 12, 1, 1),
        RC(0, 63, 34, "停止交易", 12, 1, 1),
        RC(0, 63, 50, "14:25:58", 12, 1, 1),             # 队首帧接收于时钟前 1.5s
        RC(65, 127, 2, "APRS", 12, 1, 0),                # 消息帧无位置：上格显示发给/路径
        RC(65, 127, 18, "-", 12, 1, 0),
        RC(65, 95, 34, "RSSI", 10, 1, 0),
        RC(65, 95, 50, "-92", 12, 1, 0),
        RC(97, 127, 34, "SNR", 10, 1, 0),
        RC(97, 127, 50, "12", 12, 1, 0),
        ("vline", 64, 0, 63), ("hline", 32, 64, 127), ("vline", 96, 32, 63),
    ],
    "inbox": [
        ("fill", 0, 0, 127, 11, 1),                      # 顶栏恒反显
        R(4, 0, "BG5BLB", 12, 1, 1),
        R(94, 0, "1 / 4", 12, 1, 1),
        RC(0, 127, 16, "有内鬼 停止交易", 12, 1, 0),
        R(4, 40, "14:25:58", 12, 1, 0),
        R(76, 40, "RSSI -92", 12, 1, 0),
        R(4, 52, "-", 12, 1, 0),
        R(106, 52, "OK", 12, 1, 0),
    ],
    # 导航路径：--screen idle --keys "3,2"  →  进收件箱、翻到第 2 条（BG5BLH 中继信号 正常）
    "inbox2": [
        ("fill", 0, 0, 127, 11, 1),
        R(4, 0, "BG5BLH", 12, 1, 1),
        R(94, 0, "2 / 4", 12, 1, 1),
        RC(0, 127, 16, "中继信号 正常", 12, 1, 0),
        R(4, 40, "14:25:13", 12, 1, 0),                  # 第 2 条接收于时钟前 46.5s
        R(76, 40, "RSSI -88", 12, 1, 0),
        R(4, 52, "-", 12, 1, 0),
        R(106, 52, "OK", 12, 1, 0),
    ],
}

def main():
    if len(sys.argv) < 3 or sys.argv[2] not in SPEC:
        print("用法: python tools/verify_ui.py FILE.bmp {%s}" % "|".join(SPEC))
        print("固定输入: --selftest --clock 51960 --wallclock 3,9,16 --mycall BG5BLH --batt 2 --demo")
        print("inbox2 需加 --screen idle --keys \"3,2\"")
        sys.exit(2)
    px, scale = read_bmp(sys.argv[1])
    name = sys.argv[1]
    fails = 0
    for item in SPEC[sys.argv[2]]:
        kind = item[0]
        if kind == "text":
            (_, x, y, s, tier, k, inv) = item
            bad, pol = check(px, x, y, s, tier, k, inv)
            tag = "OK " if bad == 0 else "FAIL"
            if bad:
                fails += 1
            print(f"  [{tag}] pol={pol} bad={bad:3d}  ({x:3d},{y:2d}) {s!r}")
        elif kind == "text_cx":
            (_, x0, x1, y, s, tier, k, inv) = item
            x = cx(s, tier, x0, x1, k)
            bad, pol = check(px, x, y, s, tier, k, inv)
            tag = "OK " if bad == 0 else "FAIL"
            if bad:
                fails += 1
            print(f"  [{tag}] pol={pol} bad={bad:3d}  cx[{x0},{x1}],{y:2d} {s!r}")
        elif kind == "vline":
            (_, x, y0, y1) = item
            bad = check_vline(px, x, y0, y1)
            tag = "OK " if bad == 0 else "FAIL"
            if bad:
                fails += 1
            print(f"  [{tag}] vline x={x} y={y0}..{y1} bad={bad}")
        elif kind == "hline":
            (_, y, x0, x1) = item
            bad = check_hline(px, y, x0, x1)
            tag = "OK " if bad == 0 else "FAIL"
            if bad:
                fails += 1
            print(f"  [{tag}] hline y={y} x={x0}..{x1} bad={bad}")
        elif kind == "fill":
            (_, x0, y0, x1, y1, want) = item
            bad = check_rect_fill(px, x0, y0, x1, y1, want)
            tag = "OK " if bad == 0 else "FAIL"
            if bad:
                fails += 1
            print(f"  [{tag}] fill [{x0},{y0}..{x1},{y1}] bad={bad}")
    print(f"{name}: {'全部通过' if fails == 0 else f'{fails} 项失败'}")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
