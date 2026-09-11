#!/usr/bin/env python3
"""生成 1200 baud Bell202 AFSK 的 APRS 测试音频（WAV）。

用法：
  python gen_afsk_wav.py tools/test_aprs_144.wav
  python gen_afsk_wav.py --vox tools/test_aprs_144_vox.wav

--vox：在 APRS 数据前加 100ms 触发音 + 100ms 低电平保持音，
      用于让 SunSDR/手台的 VOX 提前打开，数据包不会丢前导。
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
VOX_TRIGGER_MS = 100
VOX_DELAY_MS = 100


def tx_bits(frame: bytes):
    """flag 前导 + 位填充帧 + 尾部 flag，得到逻辑数据比特（AX.25 LSB-first）"""
    bits = []
    for _ in range(24):
        for i in range(8):
            bits.append((0x7E >> i) & 1)
    stuffed = bitstuff_bytes(frame)
    for b in stuffed:
        for i in range(8):
            bits.append((b >> i) & 1)
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


def main(out_path: str, vox: bool = False):
    frame = build_frame("APRS", "BG5BLH", b":BG5BLH   :Hello APRS 144.640")
    bits = tx_bits(frame)
    tones = nrz_i_encode(bits)

    samples_per_bit = RATE // BAUD
    phase = 0.0
    pcm = bytearray()

    if vox:
        # 100ms 触发音：让 VOX 打开
        phase = append_tone(pcm, phase, MARK_HZ, VOX_TRIGGER_MS, 0.35)
        # 100ms 低电平保持：等 PTT 完全稳定，同时不让 VOX 掉
        phase = append_tone(pcm, phase, MARK_HZ, VOX_DELAY_MS, 0.05)

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
    print("wrote", out_path, "%.3f s" % (len(pcm) / 2 / RATE), "vox=", vox)


if __name__ == "__main__":
    args = sys.argv[1:]
    vox = False
    if "--vox" in args:
        vox = True
        args.remove("--vox")
    out = args[0] if args else "tools/test_aprs_144.wav"
    main(out, vox)