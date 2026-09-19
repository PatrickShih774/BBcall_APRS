#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
aprs_log_report.py -- 从串口日志里导出"解出了哪些包"，并与参考清单对比算解码率。

用法:
  python tools/aprs_log_report.py <串口日志>                   # 用 [RAW]/[FRAME]/[DUP] 统计
  python tools/aprs_log_report.py <串口日志> --mode inbox      # 用设备 INBOX? 导出的 [INBOX] 行
  python tools/aprs_log_report.py <日志> --ref 参考清单.txt     # 与参考包对比
  python tools/aprs_log_report.py <日志> --out 结果.md          # 顺便写一份 Markdown 表格

参考清单格式（每行一条，空行/# 注释忽略）:
  :BG5BLB   :Hello APRS 144.640            <- 直接写 info 内容（推荐，和串口日志一致）
  BG5BLB|:BG5BLB   :Hello APRS 144.640     <- 也可以 "呼号|内容"

日志里认这几类行（其余忽略）:
  [RAW] len=00073 hex=...                  帧原始字节（用来算与固件一致的 16bit 哈希）
  [T=12345ms] [FRAME] src=.. dest=.. path=.. ctrl=.. info=.. RSSI=.. SNR=.. (decode-now|peak-fallback)
  [T=12345ms] [DUP] src=...
  [INBOX] i=1/13 t=HH:MM:SS src=.. dst=.. path=.. rssi=.. snr=.. f=OK r=1 h=XXXX info=...
"""
import argparse
import os
import re
import sys
from collections import OrderedDict

T_LINE = re.compile(r"^\[T=(\d+)ms\]")
RAW_LINE = re.compile(r"\[RAW\]\s+len=(\d+)\s+hex=([0-9A-Fa-f]+)")
FRAME_LINE = re.compile(r"^\[FRAME\]\s+src=(\S+)\s+dest=(\S+)\s+path=(\S+)\s+ctrl=(\S+)\s+info=(.*)$")
DUP_LINE = re.compile(r"^\[T=\d+ms\]\s+\[DUP\]\s+src=(\S+)")
INBOX_LINE = re.compile(
    r"^\[INBOX\]\s+i=(\d+)/(\d+)\s+t=(\S+)\s+src=(\S+)\s+dst=(\S+)\s+path=(\S+)\s+"
    r"rssi=(\S+)\s+snr=(\S+)\s+f=(\S+)\s+r=(\d)\s+h=([0-9A-Fa-f]+)\s+info=(.*)$")
INBOX_HEAD = re.compile(r"^\[INBOX\]\s+n=(\d+)\s+unread=(\d+)\s+dropn=(\d+)")


def frame_hash(data: bytes) -> int:
    """与固件 bbcall_app.c / ui_harness.c 一致的 16bit 整帧哈希"""
    h = 0
    for b in data:
        h = ((h << 5) ^ (h >> 2) ^ b) & 0xFFFF
    return h


def norm(s: str) -> str:
    return " ".join(s.strip().split())


def parse_log(path):
    frames, inbox, drops = [], [], {}
    last_t = 0
    last_hash = None
    with open(path, encoding="utf-8-sig", errors="ignore") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            m = T_LINE.match(line)
            if m:
                last_t = int(m.group(1))
            m = RAW_LINE.search(line)
            if m:
                try:
                    data = bytes.fromhex(m.group(2))
                    last_hash = frame_hash(data)
                except ValueError:
                    last_hash = None
                continue
            m = FRAME_LINE.match(line.strip())
            if m:
                src, dst, pth, ctrl, rest = m.groups()
                rssi = snr = None
                m2 = re.search(r"\s+RSSI=(-?\d+|--)\s+SNR=(-?\d+|--)", rest)
                info = rest
                if m2:
                    info = rest[:m2.start()]
                    rssi = None if m2.group(1) == "--" else int(m2.group(1))
                    snr = None if m2.group(2) == "--" else int(m2.group(2))
                flags = "REP" if "[REP]" in line else ("FIX" if "[FIX]" in line else "OK")
                frames.append(dict(t=last_t, src=src, dst=dst, path=pth, ctrl=ctrl,
                                   info=info, rssi=rssi, snr=snr, flags=flags, h=last_hash))
                continue
            m = DUP_LINE.match(line)
            if m:
                frames.append(dict(t=last_t, src=m.group(1), dst="-", path="-", ctrl="-",
                                   info=None, rssi=None, snr=None, flags="DUP", h=last_hash))
                continue
            m = INBOX_HEAD.match(line)
            if m:
                drops = dict(n=int(m.group(1)), unread=int(m.group(2)), dropn=int(m.group(3)))
                continue
            m = INBOX_LINE.match(line.strip())
            if m:
                i, n, t, src, dst, pth, rssi, snr, fl, r, h, info = m.groups()
                inbox.append(dict(i=int(i), n=int(n), t=t, src=src, dst=dst, path=pth,
                                  rssi=None if rssi == "--" else int(rssi),
                                  snr=None if snr == "--" else int(snr),
                                  flags=fl, read=int(r), h=h, info=info))
    return frames, inbox, drops


def cluster_count(frames):
    """把解码事件按时间聚簇：间隔 >1.5s 视为另一次发射（一次发射会被多条解调路径同时解出）"""
    ts = sorted(f["t"] for f in frames if f["flags"] != "DUP" or True)
    ts = sorted(f["t"] for f in frames)
    if not ts:
        return 0, []
    groups, cur = [], [ts[0]]
    for t in ts[1:]:
        if t - cur[-1] > 1500:
            groups.append(cur)
            cur = [t]
        else:
            cur.append(t)
    groups.append(cur)
    return len(groups), groups


def load_ref(path):
    refs = []
    with open(path, encoding="utf-8-sig", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            refs.append(line.split("|", 1)[1] if "|" in line else line)
    return refs


def main():
    try:                                  # Windows 控制台默认 GBK，会把中文报表打成乱码
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--mode", choices=["auto", "frames", "inbox"], default="auto")
    ap.add_argument("--ref")
    ap.add_argument("--out")
    args = ap.parse_args()

    frames, inbox, drops = parse_log(args.log)
    md = []

    def out(line=""):
        print(line)
        md.append(line)

    use_inbox = args.mode == "inbox" or (args.mode == "auto" and inbox and not frames)

    if use_inbox:
        out("# APRS 解码导出（来自设备 INBOX? 命令）")
        out("")
        out(f"- 收件箱条数：**{len(inbox)}**" + (f"（设备报告 n={drops.get('n')}，未读 {drops.get('unread')}，满箱丢弃未读 {drops.get('dropn')}）" if drops else ""))
        out("")
        out("| # | 时间 | 呼号 | 收件人 | 路径 | RSSI | SNR | CRC | 已读 | 正文 |")
        out("|---|---|---|---|---|---|---|---|---|---|")
        for it in sorted(inbox, key=lambda x: x["i"]):
            out(f"| {it['i']} | {it['t']} | {it['src']} | {it['dst']} | {it['path']} | "
                f"{it['rssi'] if it['rssi'] is not None else '--'} | {it['snr'] if it['snr'] is not None else '--'} | "
                f"{it['flags']} | {'是' if it['read'] else '否'} | {it['info']} |")
        uniq = OrderedDict()
        for it in inbox:
            uniq.setdefault(norm(it["info"]), []).append(it)
        decoded = list(uniq.keys())
    else:
        real = [f for f in frames if f["flags"] != "DUP"]
        n_clusters, _ = cluster_count(frames)
        out("# APRS 解码报告（来自串口日志）")
        out("")
        out(f"- 解码事件总数：**{len(frames)}**（其中 `[FRAME]` {len(real)} 条，`[DUP]` 冗余 {len(frames) - len(real)} 条）")
        out(f"- 按时间聚簇（每次发射只要有解出就算一次）：**{n_clusters}** 次")
        srcs = sorted({f["src"] for f in real})
        out(f"- 台站：{', '.join(srcs) if srcs else '(无)'}")
        fix = sum(1 for f in real if f["flags"] == "FIX")
        rep = sum(1 for f in real if f["flags"] == "REP")
        out(f"- 纠错/恢复：FIX {fix}，REP {rep}，其余为原始解出")
        out("")
        out("| # | T(ms) | 呼号 | 正文 | RSSI | SNR | CRC |")
        out("|---|---|---|---|---|---|---|")
        for i, f in enumerate(real, 1):
            out(f"| {i} | {f['t']} | {f['src']} | {f['info']} | "
                f"{f['rssi'] if f['rssi'] is not None else '--'} | {f['snr'] if f['snr'] is not None else '--'} | {f['flags']} |")
        uniq = OrderedDict()
        for f in real:
            uniq.setdefault(norm(f["info"]), []).append(f)
        decoded = list(uniq.keys())
        out("")
        out(f"- 唯一包（按内容去重）：**{len(decoded)}** 个")

    if args.ref:
        refs = [norm(r) for r in load_ref(args.ref)]
        hit = [r for r in refs if r in decoded]
        miss = [r for r in refs if r not in decoded]
        extra = [d for d in decoded if d not in refs]
        out("")
        out("## 与参考清单对比")
        out("")
        out(f"- 参考清单：**{len(refs)}** 条；解出命中 **{len(hit)}** 条；漏 **{len(miss)}** 条")
        rate = (100.0 * len(hit) / len(refs)) if refs else 0.0
        out(f"- **解码率（命中/参考）＝ {len(hit)}/{len(refs)} = {rate:.1f}%**")
        if miss:
            out("")
            out("漏掉的包：")
            for m in miss:
                out(f"  - {m}")
        if extra:
            out("")
            out("参考清单里没有、但我们解出来的包：")
            for e in extra:
                out(f"  - {e}")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write("\n".join(md) + "\n")
        print(f"\n[已写出] {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())