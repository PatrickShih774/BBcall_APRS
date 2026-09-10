#!/usr/bin/env python3
"""
BK4802 频率字生成（RX 路径）。

BK4802N 数据手册：XTAL = 21.25MHz。
FMO( BG5ESN ) BK4802.c 同款算法：接收时 PLL 锁定在 LO = RF - IF，
IF = 137kHz；value = LO_MHz * Ndiv * 2^24 / 21.25。
Ndiv 与频段选择字(reg2)对照表取自 FMO nDivCacl。
"""

XTAL_MHZ = 21.25
IF_RX_MHZ = 0.137
TWO24 = 2 ** 24


# 频段 -> (Ndiv, reg2 选择字)
BANDS = [
    (384, 512, 4, 0x0002),   # 70cm UHF
    (128, 170, 12, 0x2004),  # 2m VHF
    (43, 57, 36, 0x8008),    # 6m/低VHF
    (35, 46, 44, 0xA00A),
    (24, 32, 64, 0xC00F),
]


def pick_band(f_mhz):
    for lo, hi, n, reg2 in BANDS:
        if lo <= f_mhz <= hi:
            return n, reg2
    raise ValueError("frequency %.3f MHz out of BK4802 supported band" % f_mhz)


def freq_word_rx(f_mhz):
    n, reg2 = pick_band(f_mhz)
    lo_mhz = f_mhz - IF_RX_MHZ
    val = int(round(lo_mhz * n * TWO24 / XTAL_MHZ))
    return {
        "freq_mhz": f_mhz,
        "lo_mhz": lo_mhz,
        "n_div": n,
        "reg2": reg2,
        "reg0": (val >> 16) & 0xFFFF,
        "reg1": val & 0xFFFF,
        "value": val,
        "actual_rf_mhz": (val * XTAL_MHZ) / (n * TWO24) + IF_RX_MHZ,
    }


def main():
    for f in [144.640, 145.100, 144.390, 439.725, 438.500]:
        w = freq_word_rx(f)
        print(
            "f=%.3fMHz LO=%.3fMHz nDiv=%d reg2=0x%04X reg0=0x%04X reg1=0x%04X actualRF=%.6fMHz"
            % (f, w["lo_mhz"], w["n_div"], w["reg2"], w["reg0"], w["reg1"], w["actual_rf_mhz"])
        )


if __name__ == "__main__":
    main()
