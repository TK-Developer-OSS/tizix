#!/usr/bin/env python3
"""make_d88.py - 生イメージを PC-8801 2D の D88 形式へ包む。

usage: make_d88.py <in.raw> <out.d88> [disk-name]

2D = 40 シリンダ x 2 面 x 16 セクタ x 256B = 327680B。
入力は線形に並べ、トラック順(C0H0, C0H1, C1H0, ...)に 256B セクタへ切る。
足りない分は 0 埋め、超過はエラー。
"""
import struct
import sys

CYL, HEADS, SPT, SSZ = 40, 2, 16, 256
TRACKS = CYL * HEADS
HDR = 0x2B0                      # 32B 先頭 + 164 * 4B トラックオフセット表


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    name = (sys.argv[3] if len(sys.argv) > 3 else 'TIZIX').encode('ascii', 'replace')[:16]
    cap = TRACKS * SPT * SSZ
    if len(data) > cap:
        sys.exit('%s: %d B > 2D capacity %d B' % (sys.argv[1], len(data), cap))
    data = data.ljust(cap, b'\0')

    body = bytearray()
    offs = []
    for t in range(TRACKS):
        offs.append(HDR + len(body))
        c, h = t // HEADS, t % HEADS
        for s in range(SPT):
            # C H R N, セクタ数, 密度(0=倍密度), 削除マーク, ステータス, 予約5B, データ長
            body += struct.pack('<BBBBHBBB5xH', c, h, s + 1, 1, SPT, 0, 0, 0, SSZ)
            lin = (t * SPT + s) * SSZ
            body += data[lin:lin + SSZ]
    offs += [0] * (164 - TRACKS)

    head = name.ljust(17, b'\0') + b'\0' * 9 + b'\0' + b'\0'   # 名前17B 予約9B 書込禁止 媒体種別(0=2D)
    head += struct.pack('<I', HDR + len(body))
    head += b''.join(struct.pack('<I', o) for o in offs)
    assert len(head) == HDR
    open(sys.argv[2], 'wb').write(head + body)


if __name__ == '__main__':
    main()
