#!/usr/bin/env python3
"""生成 1200 baud Bell202 AFSK 的 APRS 测试音频（WAV）。

用法（参数都可省，默认仍是原来的 "Hello APRS 144.640" 帧）：

  # 默认帧（保持原行为）
  python gen_afsk_wav.py tools/test_aprs_144.wav
  python gen_afsk_wav.py --vox tools/test_aprs_144_vox.wav

  # 指定呼号(可带 SSID) + APRS 消息
  python gen_afsk_wav.py --src BG5BLB-12 --dest APRS --msg "有内鬼 停止交易" \
      --addressee BG5BLH -o tools/test_bg5blb12_msg.wav

  # 位置帧：随机经纬度 + 注释文字
  python gen_afsk_wav.py --src BG5BLB-12 --pos --random-pos --comment "有内鬼 停止交易" \
      -o tools/test_bg5blb12_pos.wav
  python gen_afsk_wav.py --src BG5BLB-12 --pos --lat 31.1879 --lon 121.4327 \
      --comment "有内鬼 停止交易" -o tools/pos2.wav

--vox：在 APRS 数据前加触发音 + 保持音，用于让 SunSDR/手台的 VOX 提前打开，
      数据包不会丢前导（VOX_TRIGGER_MS / VOX_DELAY_MS）。
位置帧格式与固件 aprs_parse_position() 对应：`=ddmm.hhN/dddmm.hhE>注释`。
"""
import argparse
import math
import os
import random
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ax25_reference import build_frame, bitstuff_bits, nrz_i_encode

RATE = 48000
MARK_HZ = 1200.0
SPACE_HZ = 2200.0
BAUD = 1200
VOX_TRIGGER_MS = 150
VOX_DELAY_MS = 150


def tx_bits(frame: bytes):
    """flag 前导 + 位填充帧 + 尾部 flag，得到逻辑数据比特（AX.25 LSB-first）"""
    bits = []
    for _ in range(24):
        for i in range(8):
            bits.append((0x7E >> i) & 1)
    # 位填充后必须逐位输出：按字节打包会在收尾 flag 前多出填充位（见 bitstuff_bits 注释）
    bits.extend(bitstuff_bits(frame))
    for _ in range(4):
        for i in range(8):
            bits.append((0x7E >> i) & 1)
    return bits


def append_tone(pcm, phase, freq, ms, amp):
    n = int(RATE * ms / 1000.0)
    step = 2.0 * math.pi * freq / RATE
    for _ in range(n):
        phase += step
        v = int(amp * 32767 * math.sin(phase))
        pcm += v.to_bytes(2, "little", signed=True)
    return phase


def split_call(s: str):
    """'BG5BLB-12' -> ('BG5BLB', 12)；无 SSID 时为 0"""
    call, _, ssid = s.partition("-")
    return call.upper(), (int(ssid) & 0x0F if ssid else 0)


def fmt_lat(deg: float) -> str:
    hemi = "N" if deg >= 0 else "S"
    v = abs(deg)
    d = int(v)
    return "%02d%05.2f%s" % (d, (v - d) * 60.0, hemi)


def fmt_lon(deg: float) -> str:
    hemi = "E" if deg >= 0 else "W"
    v = abs(deg)
    d = int(v)
    return "%03d%05.2f%s" % (d, (v - d) * 60.0, hemi)


def build_info(args):
    """按参数拼出 APRS 信息域，并返回可打印的说明"""
    if args.pos:
        lat = args.lat
        lon = args.lon
        if args.random_pos or lat is None or lon is None:
            rnd = random.Random(args.seed)
            lat = rnd.uniform(20.0, 50.0)      # 中国境内纬度带
            lon = rnd.uniform(100.0, 130.0)
        comment = args.comment or ""
        info = ("=" + fmt_lat(lat) + "/" + fmt_lon(lon) + ">" + comment).encode("utf-8")
        return info, "位置帧 lat=%.4f lon=%.4f 注释=%r" % (lat, lon, comment)
    text = args.msg if args.msg is not None else "Hello APRS 144.640"
    addressee = (args.addressee or args.dest).upper().ljust(9)[:9]
    info = (":" + addressee + ":" + text).encode("utf-8")
    return info, "消息帧 收件人=%s 正文=%r" % (addressee.strip(), text)


def main(out_path: str, args):
    d_call, d_ssid = split_call(args.dest)
    s_call, s_ssid = split_call(args.src)
    info, desc = build_info(args)
    frame = build_frame(d_call, s_call, info, d_ssid=d_ssid, s_ssid=s_ssid)
    bits = tx_bits(frame)
    tones = nrz_i_encode(bits)

    samples_per_bit = RATE // BAUD
    phase = 0.0
    pcm = bytearray()

    if args.vox:
        # 触发音：让 VOX 打开
        phase = append_tone(pcm, phase, MARK_HZ, VOX_TRIGGER_MS, 0.35)
        # 保持音：等 PTT 完全稳定，同时确保 VOX 不掉（0.15 幅度足够稳）
        phase = append_tone(pcm, phase, MARK_HZ, VOX_DELAY_MS, 0.15)

    for byte in tones:
        for i in range(8):
            tone = (byte >> (7 - i)) & 1
            freq = SPACE_HZ if tone else MARK_HZ
            for _ in range(samples_per_bit):
                phase += 2.0 * math.pi * freq / RATE
                v = int(0.45 * 32767 * math.sin(phase))
                pcm += v.to_bytes(2, "little", signed=True)

    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(pcm))
    print("wrote %s  %.3f s  vox=%s" % (out_path, len(pcm) / 2 / RATE, args.vox))
    print("  源 %s-%d -> 目标 %s-%d  %s" % (s_call, s_ssid, d_call, d_ssid, desc))
    print("  信息域 %d 字节: %s" % (len(info), info.decode("utf-8", "replace")))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="生成 1200 baud Bell202 AFSK 的 APRS 测试音频")
    ap.add_argument("out_pos", nargs="?", help="输出 WAV（位置参数，等价 -o）")
    ap.add_argument("-o", "--out", help="输出 WAV 路径")
    ap.add_argument("--vox", action="store_true", help="前面加 VOX 触发音 + 保持音")
    ap.add_argument("--src", default="BG5BLH", help="源呼号，可带 SSID，如 BG5BLB-12")
    ap.add_argument("--dest", default="APRS", help="目标呼号（消息帧默认也用它作收件人）")
    ap.add_argument("--msg", help="APRS 消息正文（UTF-8，中文按字节数计）")
    ap.add_argument("--addressee", help="消息收件人（默认同 --dest）")
    ap.add_argument("--pos", action="store_true", help="生成位置帧而不是消息帧")
    ap.add_argument("--comment", default="", help="位置帧的注释文字")
    ap.add_argument("--lat", type=float, help="纬度（十进制度，正值北纬）")
    ap.add_argument("--lon", type=float, help="经度（十进制度，正值东经）")
    ap.add_argument("--random-pos", action="store_true", help="随机生成经纬度（中国境内）")
    ap.add_argument("--seed", type=int, help="随机数种子（复现同一条测试帧）")
    args = ap.parse_args()
    out = args.out or args.out_pos or "tools/test_aprs_144.wav"
    main(out, args)