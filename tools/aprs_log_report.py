#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
aprs_log_report.py -- 从串口日志里导出"解出了哪些包"，并与参考清单对比算解码率。

用法:
  python tools/aprs_log_report.py <串口日志>                   # 用 [RAW]/[FRAME]/[DUP] 统计
  python tools/aprs_log_report.py <串口日志> --mode inbox      # 用设备 INBOX? 导出的 [INBOX] 行
  python tools/aprs_log_report.py <串口日志> --mode log        # 用设备累计日志 LOG? 导出的 [LOG] 行
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
  [LOG] n=13 cap=112 seq_last=13 lost=0 order=old2new          设备端累计日志的表头
  [LOG] i=1/13 t=2026-09-19 08:12:33 src=BG5BLB-12 rssi=75 snr=20 f=OK h=1A2B info=...

设备端累计日志（LOG?）的时间列有两种：RTC 已对时是 "2026-09-19 14:25:58"，
没对过时是上电秒数 "UP+1234s"（这种情况下先给设备对时，再和网络清单按时间对齐）。

--mode auto 的判定顺序：有 [LOG] i= 行 -> log；否则有 [INBOX] i= 行 -> inbox；否则 frames。
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

# 设备端累计日志（LOG?）: 开机行 / 导出表头 / 清空回显 / 每条记录
LOG_INIT = re.compile(
    r"^\[LOG\]\s+init\s+pages=(\d+)\s+record=(\d+)B\s+cap=(\d+)\s+n=(\d+)\s+seq_last=(\d+)")
LOG_HEAD = re.compile(
    r"^\[LOG\]\s+n=(\d+)\s+cap=(\d+)\s+seq_last=(\d+)\s+lost=(\d+)\s+order=(\S+)")
LOG_ERASED = re.compile(r"^\[LOG\]\s+erased\s+n=(\d+)")
LOG_FAILED = re.compile(r"^\[LOG\]\s+(append failed|not ready)")
LOG_REC = re.compile(
    r"^\[LOG\]\s+i=(\d+)/(\d+)\s+"
    r"t=(UP\+\d+s|\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+src=(\S+)\s+"
    r"rssi=(\S+)\s+snr=(\S+)\s+f=(\S+)\s+h=([0-9A-Fa-f]+)(?:\s+info=(.*))?$")


def frame_hash(data: bytes) -> int:
    """与固件 bbcall_app.c / ui_harness.c 一致的 16bit 整帧哈希"""
    h = 0
    for b in data:
        h = ((h << 5) ^ (h >> 2) ^ b) & 0xFFFF
    return h


def norm(s: str) -> str:
    return " ".join(s.strip().split())


def _num_or_dash(s):
    return None if s == "--" else int(s)


def parse_log_full(path):
    """解析一份串口日志，返回 (frames, inbox, drops, logrecs, loginfo)。

    logrecs: 设备端累计日志（LOG?）的每条记录，按 i 排序（1 = 最旧）
    loginfo: LOG? 的表头信息（n/cap/seq_last/lost/order，以及开机行的 pages/record/base 等）
    """
    frames, inbox, drops, logrecs = [], [], {}, []
    loginfo = {}
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
                                  rssi=_num_or_dash(rssi), snr=_num_or_dash(snr),
                                  flags=fl, read=int(r), h=h, info=info))
                continue
            # ---- 设备端累计日志（LOG?）----
            m = LOG_INIT.match(line.strip())
            if m:
                loginfo.update(pages=int(m.group(1)), record_size=int(m.group(2)),
                               cap=int(m.group(3)), n=int(m.group(4)),
                               seq_last=int(m.group(5)))
                continue
            m = LOG_HEAD.match(line.strip())
            if m:
                loginfo.update(n=int(m.group(1)), cap=int(m.group(2)),
                               seq_last=int(m.group(3)), lost=int(m.group(4)),
                               order=m.group(5))
                continue
            m = LOG_ERASED.match(line.strip())
            if m:
                loginfo["erased"] = int(m.group(1))
                continue
            m = LOG_FAILED.match(line.strip())
            if m:
                logrecs.append(None)      # 占位：说明导出期间设备报过 append failed / not ready
                loginfo["failed"] = loginfo.get("failed", 0) + 1
                continue
            m = LOG_REC.match(line.strip())
            if m:
                i, n, t, src, rssi, snr, fl, h, info = m.groups()
                logrecs.append(dict(i=int(i), n=int(n), t=t, src=src,
                                    rssi=_num_or_dash(rssi), snr=_num_or_dash(snr),
                                    flags=fl, h=h, info=(info or "")))
                continue
    logrecs = [r for r in logrecs if r is not None]
    logrecs.sort(key=lambda x: x["i"])
    return frames, inbox, drops, logrecs, loginfo


def parse_log(path):
    """兼容旧调用：只取 frames / inbox / drops 三件套。"""
    frames, inbox, drops, _logrecs, _loginfo = parse_log_full(path)
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
    ap.add_argument("--mode", choices=["auto", "frames", "inbox", "log"], default="auto")
    ap.add_argument("--ref")
    ap.add_argument("--out")
    args = ap.parse_args()

    frames, inbox, drops, logrecs, loginfo = parse_log_full(args.log)
    md = []

    def out(line=""):
        print(line)
        md.append(line)

    if args.mode == "log":
        use_log, use_inbox = True, False
    elif args.mode == "inbox":
        use_log, use_inbox = False, True
    elif args.mode == "frames":
        use_log, use_inbox = False, False
    else:                                  # auto: log -> inbox -> frames
        use_log = bool(logrecs)
        use_inbox = (not use_log) and bool(inbox)

    if use_log:
        out("# APRS 累计解码日志（设备端 Flash，来自 LOG? 导出）")
        out("")
        n_dev = loginfo.get("n")
        cap = loginfo.get("cap")
        seq_last = loginfo.get("seq_last")
        lost = loginfo.get("lost")
        head = f"- 记录条数：**{len(logrecs)}**"
        if n_dev is not None:
            head += f"（设备报告 n={n_dev}"
            if cap is not None:
                head += f"，容量 cap={cap}"
            if seq_last is not None:
                head += f"，seq_last={seq_last}"
            if lost is not None:
                head += f"，lost={lost}"
            head += "）"
        out(head)
        if lost:
            out(f"- **注意：设备里最旧的 {lost} 条已被新记录覆盖丢掉**（这段观测绕环了；"
                f"想全都要就加大 bbcall_cfg.h 的 BBCALL_PKTLOG_PAGES 或缩短观测时间）")
        elif lost == 0:
            out("- 覆盖丢弃：0 条（这段观测里最早的记录还在）")
        if loginfo.get("failed"):
            out(f"- **导出期间设备报过 {loginfo['failed']} 次 `[LOG] append failed/not ready`**，"
                f"下面的清单可能不完整")
        if loginfo.get("erased") is not None:
            out(f"- 日志在抓取过程中被清空过（`[LOG] erased n={loginfo['erased']}`），`LOG?` 只应查一次")
        up = [it for it in logrecs if it["t"].startswith("UP+")]
        if up:
            out(f"- **时间基准：{len(up)}/{len(logrecs)} 条是上电秒数 `UP+..s`（RTC 没对时）**，"
                f"和网络清单按绝对时间对齐前先 `set_rtc_time.ps1` 对时")
        else:
            out("- 时间基准：RTC 绝对时间（可和网络侧包清单按时间对齐）")
        srcs = sorted({it["src"] for it in logrecs})
        out(f"- 台站（{len(srcs)}）：{', '.join(srcs) if srcs else '(无)'}")
        fix = sum(1 for it in logrecs if it["flags"] == "FIX")
        rep = sum(1 for it in logrecs if it["flags"] == "REP")
        out(f"- 纠错/恢复：FIX {fix}，REP {rep}，其余为原始解出")
        out("")
        out("| # | 时间 | 呼号 | RSSI | SNR | CRC | 哈希 | 正文 |")
        out("|---|---|---|---|---|---|---|---|")
        for it in logrecs:
            out(f"| {it['i']} | {it['t']} | {it['src']} | "
                f"{it['rssi'] if it['rssi'] is not None else '--'} | "
                f"{it['snr'] if it['snr'] is not None else '--'} | "
                f"{it['flags']} | {it['h']} | {it['info']} |")
        uniq = OrderedDict()
        for it in logrecs:
            uniq.setdefault(norm(it["info"]), []).append(it)
        decoded = list(uniq.keys())
        out("")
        out(f"- 唯一包（按正文去重）：**{len(decoded)}** 个")
        repeats = [(k, len(v)) for k, v in uniq.items() if len(v) > 1]
        if repeats:
            out(f"- 其中重复发射/重复解出的正文 {len(repeats)} 个（最多的一条出现 "
                f"{max(c for _, c in repeats)} 次）")
    elif use_inbox:
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