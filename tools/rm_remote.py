#!/usr/bin/env python3
"""rm_remote.py -- build/parse the car's binary frames (A5 5A seq cmd len payload crc8).

Text commands (send as ASCII lines) are enough for daily debugging; this script is
for the binary frame protocol (0x01 VEL / 0x02 SERVO / 0x03 FIRE / 0x81 TELEM ...).

Usage:
  python rm_remote.py --selftest
  python rm_remote.py --build vel 300 0 0
  python rm_remote.py --decode "A5 5A ..."   (a captured telem frame)

No external dependencies. Python 3.6+.
"""
import argparse
import sys

SOF = bytes((0xA5, 0x5A))
CMD = {
    'vel': 0x01, 'servo': 0x02, 'fire': 0x03, 'ping': 0x04,
    'wheel': 0x05, 'flip': 0x06, 'telem': 0x07, 'stop': 0x08,
    'cal': 0x09, 'ref': 0x0A,
}
RSP = {0x81: 'TELEM', 0x82: 'PONG', 0x83: 'ACK', 0x84: 'TEXT'}


def crc8(data, crc=0):
    """poly 0x07, init 0 -- covers seq..payload (the A5 5A header is not included)."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build(cmd, payload=b'', seq=0):
    body = bytes((seq & 0xFF, CMD[cmd] if isinstance(cmd, str) else cmd, len(payload))) + payload
    return SOF + body + bytes((crc8(body),))


def i16(v):
    return int(v).to_bytes(2, 'little', signed=True)


def extract(buf):
    """Pull complete frames out of a byte stream; returns [(cmd, payload)]."""
    out = []
    i = 0
    while i + 6 <= len(buf):
        if buf[i] != 0xA5 or buf[i + 1] != 0x5A:
            i += 1
            continue
        seq, cmd, ln = buf[i + 2], buf[i + 3], buf[i + 4]
        if i + 5 + ln + 1 > len(buf):
            break
        payload = buf[i + 5:i + 5 + ln]
        if crc8(buf[i + 2:i + 5 + ln]) == buf[i + 5 + ln]:
            out.append((cmd, payload))
            i += 6 + ln
        else:
            i += 1
    return out


def decode_vision(b):
    """v3.4 遥测第 18 字节：bit0-1 模式，bit2 自动瞄准，bit3 有目标"""
    modes = {0: 'none', 1: 'rel', 2: 'abs'}
    return {
        'mode': modes.get(b & 0x03, '?'),
        'aim': bool(b & 0x04),
        'target': bool(b & 0x08),
    }


def decode_telem(payload):

    if len(payload) < 16:
        return None
    u16 = lambda o: int.from_bytes(payload[o:o + 2], 'little')
    i16s = lambda o: int.from_bytes(payload[o:o + 2], 'little', signed=True)
    u32 = lambda o: int.from_bytes(payload[o:o + 4], 'little')
    flags = payload[11]
    return {
        'vbat_mv': u16(0),
        'vx': i16s(2), 'vy': i16s(4), 'wz': i16s(6),
        'servo_deg10': i16s(8),
        'fire_state': payload[10],
        'flags': {'link': bool(flags & 1), 'low_v': bool(flags & 2),
                  'locked': bool(flags & 4), 'firing': bool(flags & 8)},
        'uptime_ms': u32(12),
        # v3.1：遥测 18 字节，末 2 字节是 Yaw；旧 16 字节帧没有这个字段
        'yaw_deg10': i16s(16) if len(payload) >= 18 else None,
        # v3.4：20 字节，第 18 字节是视觉状态
        'vision': decode_vision(payload[18]) if len(payload) >= 20 else None,
    }


def selftest():
    ok = True

    def check(name, cond):
        nonlocal ok
        print(('PASS ' if cond else 'FAIL ') + name)
        ok = ok and cond

    f = build('vel', i16(300) + i16(-200) + i16(0), seq=7)
    check('frame bytes', f[:6] == bytes((0xA5, 0x5A, 7, 0x01, 6, 0x2C)))
    check('crc matches', crc8(f[2:-1]) == f[-1])

    noise = b'\x11\x22' + build('fire', b'\x02') + b'\x00' + build('ping')
    got = extract(noise)
    check('extract 2 frames', [c for c, _ in got] == [0x03, 0x04])

    bad = bytearray(build('servo', i16(450)))
    bad[-1] ^= 0xFF
    check('bad crc dropped', extract(bytes(bad)) == [])

    payload = (b'\x00\x00' + i16(100) + i16(-50) + i16(10) + i16(900) + b'\x02' +
               bytes((0b1011,)) + (1234).to_bytes(4, 'little'))
    t = decode_telem(payload)
    check('telem decode', t['vx'] == 100 and t['wz'] == 10 and t['servo_deg10'] == 900
          and t['fire_state'] == 2 and t['uptime_ms'] == 1234 and t['flags']['firing'])
    print('ALL PASS' if ok else 'FAILURES')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--build', nargs='+', metavar='ARGS')
    ap.add_argument('--decode', nargs='+', metavar='HEX')
    a = ap.parse_args()

    if a.selftest:
        sys.exit(selftest())
    if a.build:
        name, *rest = a.build
        if name == 'vel':
            payload = i16(rest[0]) + i16(rest[1]) + i16(rest[2])
        elif name == 'servo':
            payload = i16(rest[0])
        elif name == 'fire':
            payload = bytes((int(rest[0]),))
        else:
            payload = b''
        print(' '.join('%02X' % b for b in build(name, payload)))
        return
    if a.decode:
        data = bytes(int(x, 16) for x in a.decode)
        for cmd, p in extract(data):
            print('cmd 0x%02X %-5s len=%d payload=%s' % (cmd, RSP.get(cmd, '?'), len(p), p.hex()))
            if cmd == 0x81:
                print(decode_telem(p))
        return
    ap.print_help()


if __name__ == '__main__':
    main()