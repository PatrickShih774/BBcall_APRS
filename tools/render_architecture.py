#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""渲染 BBcall_APRS 架构总览图（风格参照 CH582 架构图：芯片居中 + 四象限 + 底部引脚总表）。

输出: docs/architecture.png
数据来源: firmware-stm32porject/Core/Inc/bbcall_cfg.h（引脚）、README §2/§3（结构）
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(sys.executable).parent.parent.parent))
from daimon_runtime import setup_plot  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.patches import FancyBboxPatch, Rectangle  # noqa: E402

setup_plot()

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "architecture.png"

INK = "#1f2a3d"      # 标题深蓝
CHIP = "#262d38"     # 芯片底色
TRACE = "#8f9aa6"    # 走线
BOX_EC = "#b8bfc9"   # 模块框
HEAD = "#243b63"     # 模块标题色

fig = plt.figure(figsize=(16, 10), dpi=150)
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(0, 160)
ax.set_ylim(0, 100)
ax.axis("off")
fig.patch.set_facecolor("white")


def rbox(x, y, w, h, fc="white", ec=BOX_EC, lw=1.4, r=1.2):
    b = FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={r}",
                       fc=fc, ec=ec, lw=lw, zorder=3)
    ax.add_patch(b)
    return b


def chip_item(x, y, s, fs=9.2, fc="#f4f6f9"):
    ax.text(x, y, s, ha="center", va="center", fontsize=fs, color=INK,
            zorder=5, bbox=dict(boxstyle="round,pad=0.42", fc=fc, ec="#ccd2da", lw=0.9))


def module_box(x, y, w, h, title, groups):
    """groups: [(subtitle, [item,...]), ...] 在模块内排 n 列"""
    rbox(x, y, w, h)
    ax.text(x + w / 2, y + h - 3.2, title, ha="center", va="center",
            fontsize=13.5, fontweight="bold", color=INK, zorder=5)
    n = len(groups)
    gy = y + h - 7.2
    col_w = (w - 6) / n
    for i, (sub, items) in enumerate(groups):
        cx = x + 3 + col_w * i + col_w / 2
        if sub:
            ax.text(cx, gy - 1.0, sub, ha="center", va="center", fontsize=10.5,
                    fontweight="bold", color=HEAD, zorder=5)
        iy = gy - 4.4
        for it in items:
            chip_item(cx, iy, it, fs=9.0)
            iy -= 4.2


def trace(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    ax.plot(xs, ys, color=TRACE, lw=2.0, zorder=1, solid_capstyle="round")


# ---------------- 标题 ----------------
ax.text(80, 96.5, "BBcall_APRS 架构总览", ha="center", va="center",
        fontsize=23, fontweight="bold", color=INK)
ax.text(80, 92.8, "BK4802 APRS 寻呼机 · 仅接收 · 144.640 MHz · 规范见 design.md v2.0",
        ha="center", va="center", fontsize=10.5, color="#5a6472")

# ---------------- 中央芯片 ----------------
CX, CY, CW, CH = 80, 55, 30, 20
# 四象限走线（先画线，压在底层）
trace([(62, 74), (68, 74), (68, CY + CH / 2 + 2)])   # TL -> 芯片左上
trace([(98, 74), (92, 74), (92, CY + CH / 2 + 2)])   # TR -> 芯片右上
trace([(62, 34), (68, 34), (68, CY - CH / 2 - 2)])   # BL -> 芯片左下
trace([(98, 34), (92, 34), (92, CY - CH / 2 - 2)])   # BR -> 芯片右下
trace([(CX, CY - CH / 2), (CX, 11.5)])               # 芯片 -> 底部引脚总线

rbox(CX - CW / 2, CY - CH / 2, CW, CH, fc=CHIP, ec="#11151b", lw=1.6, r=1.0)
ax.text(CX, CY + 3.2, "STM32F103C8T6", ha="center", va="center",
        fontsize=15, fontweight="bold", color="white", zorder=5)
ax.text(CX, CY - 0.6, "Cortex-M3 · 72 MHz", ha="center", va="center",
        fontsize=9.5, color="#c8d2de", zorder=5)
ax.text(CX, CY - 3.8, "64KB Flash / 20KB RAM", ha="center", va="center",
        fontsize=9.5, color="#c8d2de", zorder=5)
# 芯片两侧的引脚翅
for i in range(7):
    yy = CY - CH / 2 + 2.2 + i * 2.4
    ax.add_patch(Rectangle((CX - CW / 2 - 1.4, yy), 1.4, 0.9, fc=CHIP, ec="#11151b", lw=0.6, zorder=2))
    ax.add_patch(Rectangle((CX + CW / 2, yy), 1.4, 0.9, fc=CHIP, ec="#11151b", lw=0.6, zorder=2))

# ---------------- 四象限模块（高 26，四行条目不溢出） ----------------
module_box(6, 66, 56, 26, "应用层", [
    ("界面（design.md v2.0）", ["ui_harness.c 三态界面", "待机 / 有未读 / 收件箱",
                                "Fusion Pixel 12/10px", "▲▼● 长按 620ms 退出"]),
    ("主循环", ["bbcall_app.c", "喂帧 / 按键扫描 / 背光", "收件箱 24 条·最新在上",
                "60s 去重 · ackNNN 分流"]),
])

module_box(98, 66, 56, 26, "信号链 · 协议", [
    ("解调 modem.c", ["Bell202 AFSK 1200bd", "16 相位 + 跳变对齐", "1/2-bit CRC 纠错",
                      "数据源 wav/replay"]),
    ("解析", ["ax25.c HDLC/CRC-16", "aprs.c 消息/位置/Mic-E", "重复包辅助恢复",
              "串口日志回放 --replay"]),
])

module_box(6, 14, 56, 26, "外设驱动", [
    ("射频 bk4802.c", ["I2C 位敲 PA9/PA10", "CE PA0 · DIO1 PA8", "RSSI/S-meter 读回",
                       "144.640MHz 仅接收"]),
    ("显示 lcd_st7567.c", ["ST7567 位敲 SPI", "Fusion 字模 374 字形 16.4KB",
                           "bbcall_hw.c 控制台·采样·IWDG"]),
])

module_box(98, 14, 56, 26, "工程与构建", [
    ("固件（CubeIDE）", [".ioc / startup / FLASH.ld", "BBCALL_LCD_ENABLED 开关", "BBCALL_MYCALL 呼号",
                        "F5 刷新工程进构建"]),
    ("PC 模拟器", ["build_win.ps1 TCC+SDL2", "与真机单源共用 ui_harness.c",
                   "verify_ui.py 四态逐像素校验"]),
])

# ---------------- 底部引脚总表 ----------------
rbox(6, 2, 148, 9.5, fc="#fbfcfe")
ax.text(9, 9.2, "引脚分配（对应 bbcall_cfg.h）", fontsize=10.5, fontweight="bold",
        color=INK, va="center", zorder=5)

pins = [
    ("射频 BK4802P", ["PA0 CE", "PA8 DIO1", "PA9 SCL", "PA10 SDA"], "#eef3fb"),
    ("音频解调", ["PA1 ADC", "TIM3 9.6kHz 采样"], "#eef7f0"),
    ("LCD ST7567", ["PB6 CS", "PB3 CLK", "PB5 MOSI", "PB4 A0", "PB7 RST", "PB0 背光"], "#f3f0fb"),
    ("调试 USART3", ["PB10 TX", "PB11 RX", "115200 8N1"], "#fdf3ec"),
    ("按键 / 指示", ["PB12 ▲", "PB13 ▼", "PB14 ●", "PB15 LED", "PA6 蜂鸣", "PA7 振动",
                     "PA2 PWR", "PA4 PTT"], "#fbeef0"),
]

def text_w(s, fs=8.2):
    """估算 chip 文本宽度（轴单位）：ASCII ~0.68、CJK ~1.15，加 pad"""
    w = 0.0
    for ch in s:
        w += 1.15 if ord(ch) > 0x2E7F else 0.68
    return w * fs / 9.0 + 1.6

px = 9.5
for title, items, fc in pins:
    ax.text(px, 7.7, title, fontsize=9, fontweight="bold", color=HEAD,
            va="center", zorder=5)
    ix = px
    for it in items:
        w = text_w(it)
        chip_item(ix + w / 2, 4.5, it, fs=8.2, fc=fc)
        ix += w + 1.6
    px = ix + 3.0

OUT.parent.mkdir(parents=True, exist_ok=True)
fig.savefig(OUT, bbox_inches="tight", facecolor="white")
print("saved:", OUT)
