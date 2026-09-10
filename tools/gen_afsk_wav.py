#!/usr/bin/env python3
"""生成 1200 baud Bell202 AFSK 的 APRS 测试音频（WAV）。
把 tools/ax25_reference.py 里的示例帧编码成 mark/space 音频，
供 BK4802/PA1 解码链路做离线自检（无需射频/第二台电台）。
"""
import math
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ax25_reference import build_frame, bitstuff_bytes, nrz_i_encode

RATE = 48000
MARK_HZ = 1200.0
SPACE_HZ = 2200.0
BAUD = 1200


def tx_bits(frame: bytes):
    """flag 前导 + 位填充帧 + 尾部 flag，得到逻辑数据比特"""
    bits = []
    for _ in range(24):                       # 前导 flag，便于接收同步
        for i in range(8):                    # AX.25 是 LSB-first
            bits.append((0x7E >> i) & 1)
    stuffed = bitstuff_bytes(frame)
    for b in stuffed:
        for i in range(8):
            bits.append((b >> i) & 1)
    for _ in range(4):
        for i in range(8):
            bits.append((0x7E >> i) & 1)
    return bits


def main(out_path: str):
    frame = build_frame("APRS", "BG5BLH", b":BG5BLH   :Hello APRS 144.640")
    bits = tx_bits(frame)
    tones = nrz_i_encode(bits)               # 0/1 音调比特流（NRZI 后）

    samples_per_bit = RATE // BAUD
    phase = 0.0
    pcm = bytearray()
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
    print("wrote", out_path, len(pcm) / RATE, "s")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "tools/test_aprs_144.wav"
    main(out)
