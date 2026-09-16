#!/usr/bin/env python3
"""
AX.25 / APRS 编解码 Python 参考。
用途：作为 STM32 C 端 modem/hdlc/ax25/aprs 的「可验证基准」。
不含任何硬件依赖，可在主机直接运行；自带 round-trip 自测。

覆盖：
  1) AX.25 UI 帧组帧（地址/控制/PID/信息 + X.25 FCS）
  2) HDLC 位填充 / 去填充
  3) NRZI 编码 / 解码
  4) APRS 消息（':'）解析
"""

import binascii


# --------------------------------------------------------------------------
# 1) X.25 FCS（CRC-16/CCITT，反映多项式 0x8408，初始 0xFFFF，结论取反）
# --------------------------------------------------------------------------
def fcs(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return (~crc) & 0xFFFF


def crc_ok(frame: bytes) -> bool:
    """校验整帧（含 FCS），标准 AX.25 残差应为 0xF0B8"""
    crc = 0xFFFF
    for b in frame:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc == 0xF0B8


# --------------------------------------------------------------------------
# 2) HDLC 位填充 / 去填充
# --------------------------------------------------------------------------
def bitstuff_bytes(data: bytes) -> bytearray:
    """对帧体（不含 flag）做位填充：连续 5 个 1 后插 0（LSB-first）"""
    out = bytearray()
    ones = 0
    acc = 0
    nbits = 0
    for b in data:
        for i in range(8):                    # AX.25 发送顺序：LSB first
            bit = (b >> i) & 1
            acc |= bit << nbits
            nbits += 1
            if nbits == 8:
                out.append(acc & 0xFF)
                acc = 0
                nbits = 0
            if bit:
                ones += 1
                if ones == 5:
                    nbits += 1                 # 插入一个 0 bit
                    if nbits == 8:
                        out.append(acc & 0xFF)
                        acc = 0
                        nbits = 0
                    ones = 0
            else:
                ones = 0
    if nbits:
        out.append(acc & 0xFF)
    return out


def bitstuff_bits(data: bytes):
    """位填充，返回**比特列表**（不按字节补齐）。

    为什么需要它：bitstuff_bytes() 把填充后的比特流按整字节打包，当填充后
    的位数不是 8 的整数倍时，收尾 flag 前会多出 0~7 个 0 位，接收端会把这
    些位当成帧内容 -> 帧长度错、CRC 校验失败。发音频必须用本函数逐位输出。
    """
    out = []
    ones = 0
    for b in data:
        for i in range(8):                    # AX.25 发送顺序：LSB first
            bit = (b >> i) & 1
            out.append(bit)
            if bit:
                ones += 1
                if ones == 5:                 # 连续 5 个 1 后插 0
                    out.append(0)
                    ones = 0
            else:
                ones = 0
    return out

def de_stuff(bits) -> bytes:
    """去填充：连续 5 个 1 后的 0 丢弃（LSB-first）"""
    data = bytearray()
    acc = 0
    nbits = 0
    ones = 0
    for bit in bits:
        if ones >= 5 and bit == 0:
            ones = 0
            continue
        acc |= bit << nbits
        nbits += 1
        if bit:
            ones += 1
        else:
            ones = 0
        if nbits == 8:
            data.append(acc & 0xFF)
            acc = 0
            nbits = 0
    if nbits:
        data.append(acc & 0xFF)
    return bytes(data)


# --------------------------------------------------------------------------
# 3) NRZI 编码 / 解码
# --------------------------------------------------------------------------
def nrz_i_encode(bits) -> bytes:
    """逻辑 0 = 跳变，逻辑 1 = 保持"""
    out = bytearray()
    acc = 0
    n = 0
    prev = 0
    for bit in bits:
        if bit == 0:
            prev ^= 1  # 跳变
        acc = (acc << 1) | prev
        n += 1
        if n == 8:
            out.append(acc & 0xFF)
            acc = 0
            n = 0
    if n:
        out.append((acc << (8 - n)) & 0xFF)
    return bytes(out)


def nrz_i_decode(tones) -> list:
    """把音调序列（0=mark,1=space）还原为数据比特。首音调作为初始化，无跳变信息。"""
    bits = []
    prev = None
    for tone in tones:
        if prev is not None:
            bits.append(0 if tone != prev else 1)  # 跳变->0
        prev = tone
    return bits


def tones_to_bits(tones):
    """音调序列(每bit) -> 数据比特流（含 flag 前景都算）"""
    return nrz_i_decode(tones)


# --------------------------------------------------------------------------
# 4) 地址字段 / 帧解析
# --------------------------------------------------------------------------
def encode_call(call: str, ssid: int) -> bytes:
    call = (call.upper()[:6]).ljust(6)
    ba = bytearray()
    for c in call:
        ba.append((ord(c) << 1) & 0xFE)
    ba.append(0x60 | ((ssid & 0x0F) << 1))  # 保留位 0110，SSID 在 bits1-4
    return bytes(ba)


def decode_call(addr7: bytes):
    call = ""
    for i in range(6):
        c = (addr7[i] >> 1) & 0x7F
        if c == 0x20:  # 空格填充终止
            break
        call += chr(c)
    ssid = (addr7[6] >> 1) & 0x0F
    return call.strip(), ssid


def build_frame(dest: str, src: str, info: bytes, d_ssid=0, s_ssid=0, path=None):
    addr = encode_call(dest, d_ssid)
    addr += encode_call(src, s_ssid)
    if path:
        for p in path:  # ('CALL', ssid)
            addr += encode_call(*p)
    # 最后一字节的扩展位=1：已是源呼号(无路径)或路径末尾
    addr = bytearray(addr)
    addr[-1] |= 0x01
    ctrl = 0x03   # UI
    pid = 0xF0
    frame = bytes(addr) + bytes([ctrl, pid]) + info
    f = fcs(frame)
    frame += bytes([f & 0xFF, (f >> 8) & 0xFF])  # FCS 低字节在前
    return frame


def parse_frame(frame: bytes):
    assert crc_ok(frame), "CRC 校验失败"
    # 去掉 FCS
    body = frame[:-2]
    # 地址段：目标(7)+源(7)，再可能若干路径
    dest, dssid = decode_call(body[0:7])
    src, sssid = decode_call(body[7:14])
    idx = 14
    path = []
    while body[idx - 1] & 0x01 == 0:  # 未到最后一个地址字节
        c, ss = decode_call(body[idx:idx + 7])
        path.append((c.strip(), ss))
        idx += 7
    ctrl = body[idx]
    pid = body[idx + 1]
    info = body[idx + 2:]
    return dest, dssid, src, sssid, path, ctrl, pid, info


def format_aprs_message(info: bytes):
    """解析 APRS 消息 ':'<9字符>':'<正文>"""
    if not info or info[0:1] != b":":
        return None
    addressee = info[1:10].decode("ascii", "replace").rstrip()
    body = info[10:].decode("utf-8", "replace") if not _is_ascii(info[11:]) else info[11:].decode("ascii", "replace")
    return addressee, body


def _is_ascii(b: bytes) -> bool:
    return all(x < 0x80 for x in b)


# --------------------------------------------------------------------------
# 自测
# --------------------------------------------------------------------------
def self_test():
    print("== AX.25/APRS 自测 ==")
    info = b":BG5BLH   :\xe4\xbd\xa0\xe5\xa5\xbd APRS"  # 你好 APRS
    frame = build_frame("APRS", "BG5BLH", info)
    ok = crc_ok(frame)
    print("build_frame len=%d crc_ok=%s" % (len(frame), ok))
    assert ok
    dest, dssid, src, sssid, path, ctrl, pid, info2 = parse_frame(frame)
    print("dest=%r src=%r path=%r ctrl=0x%02X pid=0x%02X" % (dest, src, path, ctrl, pid))
    assert dest == "APRS" and src == "BG5BLH"
    assert ctrl == 0x03 and pid == 0xF0
    m = format_aprs_message(info2)
    print("message=%r" % (m,))
    assert m is not None and m[0] == "BG5BLH"
    print("OK: 地址/CRC/APRS 消息解析一致")


if __name__ == "__main__":
    self_test()
    # 演示生成一个示例帧并打印十六进制，供示波器/C测试使用
    demo = build_frame("APRS", "BG5BLH", b":BG5BLH   :Hello APRS 144.640")
    print("demo hex:", binascii.hexlify(demo).decode())
