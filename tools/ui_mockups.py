#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ui_mockups.py -- 用固件真实字模渲染 128x64 界面方案预览（不改任何固件/界面代码）

输出到 ui_previews/：每个方案一张三联图（home / inbox / detail），4 倍放大。
预览的每一个字形、线宽、反显块都是 lcd_st7567.c 现有原语能画出来的。
"""
import os
import re

W, H = 128, 64
OUT = "ui_previews"

# ------------------------------------------------------------ 字模（与 verify_ui 同法）
def parse_font(path, h):
    src = open(path, encoding="utf-8", errors="ignore").read()
    m = re.search(r"font\w*\s*\[[0-9]+\]\s*\[[0-9]+\]\s*=\s*\{(.*)\};", src, re.S)
    data = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))]
    return {0x20 + i: data[i * h:(i + 1) * h] for i in range(len(data) // h)}

F6 = parse_font("firmware-stm32porject/Core/Inc/font6x8.h", 8)
F8 = parse_font("firmware-stm32porject/Core/Inc/font8x16.h", 16)

# ------------------------------------------------------------ 帧缓冲与原语（等价 lcd_st7567.c）
fb = bytearray(W * H)

def clear():
    for i in range(len(fb)):
        fb[i] = 0

def pixel(x, y, v=1):
    if 0 <= x < W and 0 <= y < H:
        fb[y * W + x] = v

def fill_rect(x0, y0, x1, y1, v=1):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            fb[y * W + x] = v

def hline(x0, x1, y, v=1):
    for x in range(x0, x1 + 1):
        pixel(x, y, v)

def vline(x, y0, y1, v=1):
    for y in range(y0, y1 + 1):
        pixel(x, y, v)

def rect(x0, y0, x1, y1, v=1):
    hline(x0, x1, y0, v); hline(x0, x1, y1, v)
    vline(x0, y0, y1, v); vline(x1, y0, y1, v)

def text6(x, y, s, ink=1):
    for ci, ch in enumerate(s):
        g = F6.get(ord(ch), F6[0x20])
        for r in range(8):
            for c in range(6):
                if g[r] & (0x80 >> c):
                    pixel(x + ci * 6 + c, y + r, ink)

def text6r(y, s, ink=1):
    x = (128 - len(s) * 6) // 6 * 6
    text6(x, y, s, ink)

def text8(x, y, s, ink=1):
    for ci, ch in enumerate(s):
        g = F8.get(ord(ch), F8[0x20])
        for r in range(16):
            for c in range(8):
                if g[r] & (0x80 >> c):
                    pixel(x + ci * 8 + c, y + r, ink)

def text8x2(x, y, s, ink=1):
    for ci, ch in enumerate(s):
        g = F8.get(ord(ch), F8[0x20])
        for r in range(16):
            for c in range(8):
                if g[r] & (0x80 >> c):
                    fill_rect(x + (ci * 8 + c) * 2, y + r * 2,
                              x + (ci * 8 + c) * 2 + 1, y + r * 2 + 1, ink)

def dotted_h(y, x0=0, x1=127):
    for x in range(x0, x1 + 1, 4):
        hline(x, min(x + 1, x1), y, 1)

# ------------------------------------------------------------ 共享数据（与回放日志一致的语义）
FREQ = "144.640"
CALLS = [("BD4BE", "C", "1h"), ("BD4SDX", "P", "1h"), ("BH4FSK", "C", "1h"),
         ("BG4AIZ", "C", "1h"), ("BD4BE", "C", "1h"), ("BI4BKX", "C", "1h")]

def icon_signal(x, y, bars, ink=1):
    hs = [3, 5, 7, 8]
    for i in range(min(bars, 4)):
        for k in range(hs[i]):
            fill_rect(x + i * 2, y + 7 - k, x + i * 2 + 1, y + 7 - k, ink)

def icon_mail(x, y, ink=1):
    rect(x, y + 1, x + 7, y + 6, ink)
    for i in range(3):
        pixel(x + 1 + i, y + 2 + i, ink); pixel(x + 6 - i, y + 2 + i, ink)

def smeter_bars(x, y, slevel, ink=1):
    """横向 20 格 S 表，y 为顶行，高 6px"""
    lit = int(round(slevel / 9 * 20))
    for i in range(20):
        x0 = x + i * 6
        if i < lit:
            fill_rect(x0, y, x0 + 4, y + 5, ink)
        else:
            rect(x0, y, x0 + 4, y + 5, ink)

def save(name, *frames):
    """把若干 128x64 帧横向拼成一张图，4 倍放大，帧间 8px 留白"""
    from PIL import Image
    scale = 4
    gap = 8
    img = Image.new("1", (len(frames) * (W * scale + gap) - gap, H * scale), 1)
    for fi, fr in enumerate(frames):
        one = Image.new("1", (W, H), 0)
        one.putdata([1 - p for p in fr])
        one = one.resize((W * scale, H * scale), Image.NEAREST)
        img.paste(one, (fi * (W * scale + gap), 0))
    img = img.convert("L")
    os.makedirs(OUT, exist_ok=True)
    img.save(f"{OUT}/{name}.png")
    print(f"  {OUT}/{name}.png")

# ============================================================ 方案 A：HAM 仪表
def a_home():
    clear()
    fill_rect(0, 0, 127, 7, 1)
    text6(6, 0, "BBCALL", 0)
    text6(66, 0, "RX 144.640", 0)
    hline(0, 127, 8)
    text8(6, 12, "144.640 MHz")
    text6(6, 32, "S", 1)
    smeter_bars(18, 30, 4)
    text6(6, 40, "RSSI 72    SNR 19", 1)
    text6(6, 48, "AFC  14    EXN 315", 1)
    fill_rect(0, 56, 127, 63, 1)
    text6(6, 56, "RX 13", 0)
    text6(60, 56, "DUP 0", 0)
    text6(90, 56, "MSG 13", 0)
    return bytes(fb)

def a_inbox():
    clear()
    fill_rect(0, 0, 127, 7, 1)
    text6(6, 0, "INBOX", 0)
    text6(90, 0, "13 NEW", 0)
    hline(0, 127, 8)
    # 表头 + 列分隔线（仪表读数表格）
    text6(6, 11, "CALL", 1);  vline(44, 10, 19)
    text6(48, 11, "T", 1);    vline(56, 10, 19)
    text6(60, 11, "AGE", 1);  vline(80, 10, 19)
    text6(84, 11, "INFO", 1)
    hline(0, 127, 20)
    ys = [24, 32, 40, 48, 56]
    rows = CALLS[:5]
    for i, (call, t, age) in enumerate(rows):
        y = ys[i]
        sel = (i == 0)
        if sel:
            fill_rect(0, y - 1, 127, y + 7, 1)
        ink = 0 if sel else 1
        text6(6, y, f"{call:<6}", ink)
        text6(48, y, t, ink)
        text6(60, y, age, ink)
        text6(84, y, "3111.28" if t == "C" else "3054.31", ink)
    return bytes(fb)

def a_detail():
    clear()
    fill_rect(0, 0, 127, 17, 1)
    text8(6, 1, "BD4BE  MIC-E", 0)
    hline(0, 127, 18)
    for i, s in enumerate(["3111.28N 12125.77E", "M0: Off Duty 2km/h", "223 `\"5f}_$",
                            "VIA BH4ERY*,WIDE1", "*,BG5GNC*,WIDE2"]):
        text6(6, 22 + i * 8, s, 1)
    fill_rect(0, 56, 127, 63, 1)
    text6(6, 56, "1/1", 0)
    text6(30, 56, "AGE 1h", 0)
    text6(90, 56, "RELAY", 0)
    return bytes(fb)

# ============================================================ 方案 B：现代极简（Pebble / e-ink 风）
def b_home():
    clear()
    text6(6, 5, "STANDBY", 1)
    text8x2(24, 10, "01 12")
    text6(6, 45, "144.640 MHz", 1)
    text6(78, 45, "13 new", 1)
    hline(0, 127, 54)
    text6(6, 56, "S4", 1)
    icon_signal(20, 56, 2)
    icon_mail(110, 56, 1)
    text6(92, 56, "13", 1)
    return bytes(fb)

def b_inbox():
    clear()
    text6(6, 4, "INBOX / 13 NEW", 1)
    hline(0, 127, 13)
    ys = [16, 32, 48]
    rows = CALLS[:3]
    for i, (call, t, age) in enumerate(rows):
        y = ys[i]
        if i == 0:
            fill_rect(0, y - 2, 1, y + 13, 1)          # 左侧 2px 阅读标记
        text8(8, y, call, 1)
        text6(60, y + 4, t, 1)
        text6(100, y + 4, age, 1)
    return bytes(fb)

def b_detail():
    clear()
    text6(6, 6, "MESSAGE", 1)
    text8(6, 16, "BD4BE", 1)
    text6(60, 20, "MIC-E", 1)
    text6(100, 20, "1h", 1)
    hline(0, 127, 34)
    text6(6, 42, "Hello APRS 144.640", 1)
    hline(0, 127, 54)
    text6(6, 56, "NOW", 1)
    text6(60, 56, "RELAY", 1)
    return bytes(fb)

# ============================================================ 方案 C：复古 BB 机加强（双线框 + 点阵）
def c_frame(title):
    rect(0, 0, 127, 63)
    rect(2, 2, 125, 61)
    fill_rect(4, 4, 123, 11, 1)
    text6(8, 4, title, 0)

def c_home():
    clear()
    c_frame("STANDBY")
    icon_signal(96, 4, 3, 0)
    icon_mail(114, 4, 0)
    text8x2(20, 14, "01 12")
    dotted_h(48, 6, 121)
    text6(8, 53, FREQ, 1)
    text6(80, 53, "S4 RX13", 1)
    return bytes(fb)

def c_inbox():
    clear()
    c_frame("INBOX 1/13")
    text6(96, 4, "13", 0)
    ys = [15, 23, 31, 39, 47]
    for i, (call, t, age) in enumerate(rows := CALLS[:5]):
        y = ys[i]
        s = f"{call:<6} {t} {age}"
        if i == 0:
            fill_rect(4, y - 1, 123, y + 7, 1)
            text6(8, y, s, 0)
        else:
            text6(8, y, s, 1)
        if i < 4:
            dotted_h(y + 8, 6, 121)
    return bytes(fb)

def c_detail():
    clear()
    c_frame("BD4BE MIC-E")
    for i, s in enumerate(["3111.28N 12125.77E", "M0: Off Duty 2km/h", "223 `\"5f}_$",
                            "VIA BH4ERY*,WIDE1"]):
        text6(8, 14 + i * 8, s, 1)
    dotted_h(48, 6, 121)
    text6(8, 53, "1/1 1h", 1)
    text6(80, 53, "RELAY", 1)
    return bytes(fb)


# ============================================================ 方案 D：左右分栏（左常驻轨 + 右内容区）
RAIL_W = 24
def d_rail(icon_fn=None, extra=None):
    """全高反显左轨 x=0..23；icon_fn(x,y,ink) 画顶部图标"""
    fill_rect(0, 0, RAIL_W - 1, 63, 1)
    if icon_fn:
        icon_fn(8, 4, 0)
    text6(7, 16, "13", 0)          # 未读数
    icon_mail(8, 28, 0)
    text6(7, 54, "S4", 0)

def d_home():
    clear()
    d_rail(lambda x, y, ink: icon_signal(x - 4, y, 3, ink))
    text8x2(28, 8, "01 12")
    text6(28, 44, "144.640 MHz", 1)
    text6(28, 54, "BD4BE C 1h", 1)   # 最近一帧
    return bytes(fb)

def d_inbox():
    clear()
    d_rail(lambda x, y, ink: icon_mail(x - 4, y, ink))
    text6(28, 6, "INBOX 1/13", 1)
    ys = [16, 24, 32, 40, 48, 56]
    for i, (call, t, age) in enumerate(CALLS[:6]):
        y = ys[i]
        sel = (i == 0)
        if sel:
            fill_rect(28, y - 1, 127, y + 7, 1)
        ink = 0 if sel else 1
        mark = '*' if i % 3 != 1 else ' '
        text6(28, y, f"{mark}{call:<6} {t} {age}", ink)
    return bytes(fb)

def d_detail():
    clear()
    d_rail(lambda x, y, ink: icon_signal(x - 4, y, 3, ink))
    text8(28, 4, "BD4BE", 1)
    text6(76, 8, "MIC-E 1h", 1)
    for i, s in enumerate(["3111.28N 12125.7", "M0: Off Duty 2km/", "223 `\"5f}_$",
                            "VIA BH4ERY*,WIDE1"]):
        text6(28, 24 + i * 8, s, 1)
    text6(28, 58, "1/1 1h", 1)
    text6(76, 58, "RELAY", 1)
    return bytes(fb)

# ============================================================ 方案 E：磁贴（Metro；墨量=状态）
def e_tile(x, y, w, h, filled, selected=False):
    if filled:
        fill_rect(x, y, x + w - 1, y + h - 1, 1)
        if selected:
            rect(x + 2, y + 2, x + w - 3, y + h - 3, 0)
    else:
        rect(x, y, x + w - 1, y + h - 1, 1)
        if selected:
            fill_rect(x + 2, y + 2, x + w - 3, y + h - 3, 1)

def e_home():
    clear()
    # 全宽大磁贴：大时钟（挖字）
    fill_rect(4, 4, 123, 43, 1)
    text8x2(24, 7, "01 12", 0)
    # 下方两个半宽小贴：未读（实心）+ 频率（空心）
    e_tile(4, 48, 60, 13, True)
    text6(8, 50, "INBOX 13", 0)
    e_tile(68, 48, 56, 13, False)
    text6(72, 50, "144.640", 1)
    return bytes(fb)

def e_inbox():
    clear()
    # 整行宽磁贴 x 3：未读=实心贴(挖字)，已读=空心贴；选中加内框
    pos = [4, 24, 44]
    for i, (call, t, age) in enumerate(CALLS[:3]):
        x, y = 4, pos[i]
        unread = (i % 2 == 0)          # 演示：交替出未读/已读两种贴
        e_tile(x, y, 120, 17, unread, selected=(i == 0))
        ink = 0 if unread else 1
        text6(x + 4, y + 2, f"{call} {t}", ink)
        text6(52, y + 2, f"3111.28N {age}", ink)
    return bytes(fb)

def e_detail():
    clear()
    # 头部大贴：呼号 + 类型（挖字 8x16）
    fill_rect(4, 4, 123, 21, 1)
    text8(8, 5, "BD4BE", 0)
    text6(60, 9, "MIC-E 1h", 0)
    # 正文直接落地（Metro 正文区不用框）
    for i, s in enumerate(["3111.28N 12125.77E", "M0: Off Duty 2km/h", "223 `\"5f}_$",
                            "VIA BH4ERY*,WIDE1", "*,BG5GNC*,WIDE2"]):
        text6(4, 24 + i * 8, s, 1)
    return bytes(fb)



# ============================================================ 方案 F（定稿）：D 骨架 + 息屏磁贴 + 收件箱满幅高密度
def f_rail_status():
    """一级屏：状态轨（信号/未读/信封/S 表）"""
    fill_rect(0, 0, 23, 63, 1)
    icon_signal(4, 4, 3, 0)
    text6(7, 16, "13", 0)
    icon_mail(8, 28, 0)
    text6(7, 54, "S4", 0)

def f_rail_section(glyph, label):
    """二级页：单图标轨（层级区分：没有未读数等状态集群）"""
    fill_rect(0, 0, 23, 63, 1)
    text8(8, 12, glyph, 0)
    text6(0, 30, label, 0)

def f_home():
    clear()
    f_rail_status()
    # 大时钟磁贴（实心贴 + 挖字）
    fill_rect(28, 4, 123, 41, 1)
    text8x2(38, 7, "01 12", 0)
    text6(28, 45, "144.640 MHz  S4", 1)
    # 底部两个磁贴：未读（实心） + 计数（空心）
    fill_rect(28, 54, 80, 63, 1)
    text6(32, 55, "INBOX 13", 0)
    rect(85, 54, 123, 63, 1)
    text6(89, 55, "RX 13", 1)
    return bytes(fb)

def f_inbox():
    clear()
    f_rail_status()
    # 满幅 8 行高密度列表；未读=实心行，选中=内框/外框
    data = [("BD4BE",  "C", "3111.28N", 1, 1),
            ("BD4SDX", "P", "3054.31N", 1, 0),
            ("BH4FSK", "C", "3037.90N", 0, 0),
            ("BG4AIZ", "C", "3137.15N", 0, 0),
            ("BD4BE",  "C", "3111.28N", 1, 0),
            ("BI4BKX", "C", "3116.01N", 0, 0),
            ("BH4XYZ", "M", "Hello A", 1, 0),
            ("BG4ZZZ", "X", "STATUS  ", 0, 0)]
    for i, (call, t, title, unread, sel) in enumerate(data):
        y = i * 8
        if unread:
            fill_rect(28, y, 127, y + 7, 1)
        ink = 0 if unread else 1
        text6(28, y, f"{call:<6} {t} {title:.7}", ink)
        if sel:
            if unread:
                rect(30, y + 1, 125, y + 6, 0)   # 选中未读：实心行 + 白色内框
            else:
                rect(28, y, 127, y + 7, 1)        # 选中已读：外框
    return bytes(fb)

def f_detail():
    clear()
    f_rail_section("C", "READ")
    text6(28, 2, "INBOX>BD4BE", 1)
    hline(28, 127, 11)
    for i, s in enumerate(["3111.28N 12125.7", "M0: Off Duty 2km/", "223 `\"5f}_$",
                            "VIA BH4ERY*,WIDE", "1 BG5GNC*,WIDE2"]):
        text6(28, 14 + i * 8, s, 1)
    text6(28, 58, "1/1 1h", 1)
    text6(88, 58, "RELAY", 1)
    return bytes(fb)

def f_menu():
    clear()
    f_rail_section("-", "MENU")
    for i, item in enumerate(["Radio", "Contrast", "Backlight", "About"]):
        y = 10 + i * 10
        if i == 0:
            fill_rect(28, y - 1, 127, y + 7, 1)
            text6(32, y, item, 0)
        else:
            text6(32, y, item, 1)
    return bytes(fb)



# ============================================================ 方案 G（用户定稿）：息屏=纯磁贴全屏；收件箱=磁贴主题高密度
def g_standby():
    """息屏：纯磁贴全屏，无左轨（E 原案）"""
    clear()
    fill_rect(4, 4, 123, 43, 1)          # 大时钟贴
    text8x2(24, 7, "01 12", 0)
    fill_rect(4, 48, 63, 60, 1)          # 未读贴（实心）
    text6(8, 50, "INBOX 13", 0)
    rect(68, 48, 123, 60, 1)             # 频率贴（空心）
    text6(72, 50, "144.640", 1)
    return bytes(fb)

G_DATA = [("BD4BE",  "C", "3111.28N", 1, 1),
          ("BD4SDX", "P", "3054.31N", 1, 0),
          ("BH4FSK", "C", "3037.90N", 0, 0),
          ("BG4AIZ", "C", "3137.15N", 0, 0),
          ("BD4BE",  "C", "3111.28N", 1, 0),
          ("BI4BKX", "C", "3116.01N", 0, 0),
          ("BH4XYZ", "M", "Hello AP", 1, 0)]

def g_inbox_rows():
    """候选 G1：整行磁贴条——每行一块贴，7 行/屏，未读实心"""
    clear()
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:7]):
        y = 1 + i * 9            # 行块 9px：贴框 y..y+8，文字下移 1px 避免与边框同排
        if unread:
            fill_rect(0, y, 127, y + 8, 1)
        else:
            rect(0, y, 127, y + 8, 1)
        ink = 0 if unread else 1
        text6(4, y + 1, f"{call:<6} {t} {title:.7}", ink)
        if sel:
            rect(2, y + 1, 125, y + 7, 0 if unread else 1)
    return bytes(fb)

def g_inbox_grid():
    """候选 G2：双行小贴——2 列 x 3 行，每贴两行（呼号+类型 / 摘要）"""
    clear()
    pos = [(4, 4), (68, 4), (4, 24), (68, 24), (4, 44), (68, 44)]
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:6]):
        x, y = pos[i]
        if unread:
            fill_rect(x, y, x + 59, y + 18, 1)
        else:
            rect(x, y, x + 59, y + 18, 1)
        ink = 0 if unread else 1
        text6(x + 4, y + 2, f"{call} {t}", ink)
        text6(x + 40, y + 2, "1h", ink)
        text6(x + 4, y + 10, title, ink)
        if sel:
            rect(x + 2, y + 2, x + 57, y + 16, 0 if unread else 1)
    return bytes(fb)


def g1b_inbox_rows():
    """候选 G1b：整行磁贴条 6 行版——块 9px、行距 10px，行间留 1px 缝隙找回块轮廓"""
    clear()
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:6]):
        y = 2 + i * 10           # 行块 y..y+8，缝隙 1px
        if unread:
            fill_rect(0, y, 127, y + 8, 1)
        else:
            rect(0, y, 127, y + 8, 1)
        ink = 0 if unread else 1
        text6(4, y + 1, f"{call:<6} {t} {title:.7}", ink)
        if sel:
            rect(2, y + 1, 125, y + 7, 0 if unread else 1)
    return bytes(fb)


def g2b_inbox_grid():
    """候选 G2b：G2 网格 + 顶部实心标题栏标示 INBOX 页面"""
    clear()
    # 标题栏：实心条 10px，挖字 "INBOX 13"
    fill_rect(0, 0, 127, 9, 1)
    text6(4, 1, "INBOX 13", 0)
    text6(84, 1, "01:12", 0)     # 右上角小时钟
    # 贴 60x16：行 y=12/30/48，贴内两行 6x8 字
    pos = [(4, 12), (68, 12), (4, 30), (68, 30), (4, 48), (68, 48)]
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:6]):
        x, y = pos[i]
        if unread:
            fill_rect(x, y, x + 59, y + 15, 1)
        else:
            rect(x, y, x + 59, y + 15, 1)
        ink = 0 if unread else 1
        text6(x + 4, y + 1, f"{call} {t}", ink)
        text6(x + 40, y + 1, "1h", ink)
        text6(x + 4, y + 8, title, ink)
        if sel:
            rect(x + 2, y + 1, x + 57, y + 14, 0 if unread else 1)
    return bytes(fb)


def g2c_inbox_grid4():
    """候选 G2c：16px 标题栏（8x16 大字 y=0 起）+ 2x2 加高贴 60x20，贴内文字留出上下边距"""
    clear()
    # 标题栏：实心 16px，8x16 大字恰好填满
    fill_rect(0, 0, 127, 15, 1)
    text8(4, 0, "INBOX 13", 0)
    text8(84, 0, "01:12", 0)
    # 贴 58x20：左右边距各 4、列间距 4，x=4 / x=66
    pos = [(4, 20), (66, 20), (4, 44), (66, 44)]
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:4]):
        x, y = pos[i]
        if unread:
            fill_rect(x, y, x + 57, y + 19, 1)
        else:
            rect(x, y, x + 57, y + 19, 1)
        ink = 0 if unread else 1
        text6(x + 4, y + 2, f"{call} {t}", ink)
        text6(x + 38, y + 2, "1h", ink)
        text6(x + 4, y + 11, title, ink)
        if sel:
            rect(x + 2, y + 2, x + 55, y + 17, 0 if unread else 1)
    return bytes(fb)


def g3a_inbox_ink():
    """候选 G3a：墨量分层——未读=实心整行条(挖字)，已读=纯文字行(无框)，选中=行内框"""
    clear()
    fill_rect(0, 0, 127, 15, 1)
    text8(4, 0, "INBOX 13", 0)
    text8(84, 0, "01:12", 0)
    # 4 整行：块高 10、行距 11，x=4..123；未读实心条，已读无框
    # 行内：左 呼号+类型，中 摘要，右 年龄（三列排满整行）
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:4]):
        y = 20 + i * 11
        if unread:
            fill_rect(4, y, 123, y + 9, 1)
        ink = 0 if unread else 1
        text6(8, y + 1, f"{call} {t}", ink)
        text6(62, y + 1, title[:7], ink)
        text6(105, y + 1, "1h", ink)
        if sel:
            rect(6, y + 1, 121, y + 8, 0 if unread else 1)
    return bytes(fb)


def g3c_inbox_grid4min():
    """候选 G3c：G2c 四贴网格减元素——去类型字母，贴内只留 呼号+年龄 / 摘要"""
    clear()
    fill_rect(0, 0, 127, 15, 1)
    text8(4, 0, "INBOX 13", 0)
    text8(84, 0, "01:12", 0)
    pos = [(4, 20), (66, 20), (4, 44), (66, 44)]
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:4]):
        x, y = pos[i]
        if unread:
            fill_rect(x, y, x + 57, y + 19, 1)
        else:
            rect(x, y, x + 57, y + 19, 1)
        ink = 0 if unread else 1
        text6(x + 4, y + 2, call, ink)
        text6(x + 38, y + 2, "1h", ink)
        text6(x + 4, y + 11, title, ink)
        if sel:
            rect(x + 2, y + 2, x + 55, y + 17, 0 if unread else 1)
    return bytes(fb)


def g4_inbox_ref():
    """候选 G4：仿参考图——全屏纯线框零实心。
    空心标题栏(页码加框+未读数) + 2x2 空心贴(呼号/时间) + 底部按键提示贴；未读=NEW 徽标"""
    clear()
    # 标题栏：空心通栏 y0..10
    rect(0, 0, 127, 10, 1)
    text6(4, 2, "INBOX", 1)
    rect(84, 2, 105, 9, 1)          # 页码小框
    text6(86, 2, "3/8", 1)
    text6(110, 2, "N:3", 1)         # 未读数
    # 2x2 空心贴 58x18：行 y=13/32
    pos = [(4, 13), (66, 13), (4, 32), (66, 32)]
    for i, (call, t, title, unread, sel) in enumerate(G_DATA[:4]):
        x, y = pos[i]
        rect(x, y, x + 57, y + 17, 1)
        text6(x + 4, y + 1, call, 1)
        text6(x + 4, y + 9, "14:21", 1)          # 接收时间
        text6(x + 38, y + 9, "NEW" if unread else "1h", 1)
        if sel:
            rect(x + 2, y + 1, x + 55, y + 15, 1)
    # 底部按键提示贴
    rect(4, 52, 61, 63, 1)
    text6(18, 54, "UP/DN", 1)
    rect(66, 52, 123, 63, 1)
    text6(74, 54, "OK=OPEN", 1)
    return bytes(fb)


# ============================================================
if __name__ == "__main__":
    print("渲染方案预览 ...")
    save("A_ham_instrument", a_home(), a_inbox(), a_detail())
    save("B_minimal", b_home(), b_inbox(), b_detail())
    save("C_neopager", c_home(), c_inbox(), c_detail())
    save("D_split_rail", d_home(), d_inbox(), d_detail())
    save("E_tiles", e_home(), e_inbox(), e_detail())
    save("F_final", f_home(), f_inbox(), f_detail(), f_menu())
    save("G_lockscreen", g_standby())
    save("G1_inbox_rows", g_inbox_rows())
    save("G1b_inbox_rows6", g1b_inbox_rows())
    save("G2_inbox_grid", g_inbox_grid())
    save("G2b_inbox_grid_title", g2b_inbox_grid())
    save("G2c_inbox_grid4", g2c_inbox_grid4())
    save("G3a_inbox_ink", g3a_inbox_ink())
    save("G3c_inbox_grid4min", g3c_inbox_grid4min())
    save("G4_inbox_ref", g4_inbox_ref())
    print("完成")
