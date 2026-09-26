#!/usr/bin/env python3
# ovllink.py ― ovlsplit.py で割ったコマンドを「core .bin + NN.ovl」にリンクする (tizix #69)
#
#   1 つの .rel(core の _CODE/_DATA + _OV1.._OVn)を、area の置き場所だけ
#   変えて何度かリンクし直す:
#     link 0   : 全 _OVk を 0xC000(捨て場)に置き、core の終端を測る
#     link core: ___ovlbase を確定させて core を作る
#     link k   : _OVk だけを ___ovlbase に置き、その範囲を <cmd>kk.ovl として切り出す
#   core は _OVk のラベルを一切参照しない(ovlsplit.py が検査済み)ので、
#   **どのリンクでも core のバイト列は同一**になる。ここでそれを実際に比較して
#   確かめる(ずれたら「オーバーレイが別の core 向けに貼られた」事故になる)。
#
#   ■ 領域の置き場所 ── スタックとの関係
#     kexec は argv / 初期 SP をプロセス全体の最上端(追加ブロック側)に置く。
#     像側の [core 終端, imgtop) はファイルからは何も読まれない空き RAM なので、
#     ここをオーバーレイ領域にする。スタックは追加ブロックの上端から下へ伸び、
#     途中に本文バッファ等があるので、領域へ届くのはそれを全部突き抜けたとき
#     だけ。よってビルド時に保証すべきは次の 2 つ:
#       (a) ___ovlbase >= core 終端                  (core を踏まない)
#       (b) ___ovlbase + 最大オーバーレイ <= imgtop   (追加ブロックを踏まない)
#     imgtop は kexec と同じ式 ceil((ファイル長 + 0x140) / 4096) * 4096 で求める。
#
#   使い方:
#     python3 ovllink.py --ld LD --dir DIR --cmd vi --novl N --xblk 1
#                        --crt0 crt0_tizix.rel --libdir BUILD -- <追加の -g / -b 等>
import re
import subprocess
import sys

JUNK = 0xC000
BLK = 0x1000
RESV = 0x140


def die(msg):
    sys.stderr.write('ovllink.py: ' + msg + '\n')
    sys.exit(1)


def read_ihx(path):
    img = {}
    hi = 0
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln.startswith(':'):
                continue
            b = bytes.fromhex(ln[1:])
            n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
            if typ == 4:
                hi = (b[4] << 8) | b[5]
            elif typ == 0:
                base = (hi << 16) | addr
                for i in range(n):
                    img[base + i] = b[4 + i]
    return img


def slice_img(img, lo, hi):
    return bytes(img.get(a, 0) for a in range(lo, hi))


def read_areas(path):
    areas = {}
    rx = re.compile(r'^(_\w+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s')
    with open(path) as f:
        for ln in f:
            m = rx.match(ln)
            if m:
                areas[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return areas


def link(o, tag, bases, ovlbase):
    ihx = '%s/%s.%s.ihx' % (o.dir, o.cmd, tag)
    args = [o.ld, '-n', '-m', '-i', '-b', '_CODE=0x0000']
    for k in range(1, o.novl + 1):
        args += ['-b', '_OV%d=0x%04X' % (k, bases.get(k, JUNK))]
    args += ['-g', '___ovlbase=0x%04X' % ovlbase]
    args += o.extra
    args += [ihx, o.crt0, '%s/%s.rel' % (o.dir, o.cmd)] + o.rels
    args += ['-k', o.libdir, '-l', 'libtzc.lib']
    r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True)
    if r.returncode != 0 or 'rror' in r.stdout:
        sys.stderr.write(' '.join(args) + '\n' + r.stdout)
        die('link %s failed' % tag)
    return read_ihx(ihx), read_areas(ihx[:-4] + '.map')


class Opt:
    pass


def main():
    o = Opt()
    o.ld = o.dir = o.cmd = o.crt0 = o.libdir = None
    o.novl = 0
    o.xblk = 0
    o.rels = []
    o.extra = []
    a = sys.argv[1:]
    i = 0
    while i < len(a):
        k = a[i]
        if k == '--':
            o.extra = a[i + 1:]
            break
        v = a[i + 1]
        if k == '--ld': o.ld = v
        elif k == '--dir': o.dir = v
        elif k == '--cmd': o.cmd = v
        elif k == '--novl': o.novl = int(v)
        elif k == '--xblk': o.xblk = int(v)
        elif k == '--crt0': o.crt0 = v
        elif k == '--libdir': o.libdir = v
        elif k == '--rel': o.rels.append(v)
        else: die('unknown option ' + k)
        i += 2

    # ---- link 0: core の終端を測る ----
    img0, ar0 = link(o, 'l0', {}, 0)
    s_data, l_data = ar0['_DATA']
    core_end = s_data + l_data
    for name, (s, l) in ar0.items():
        if name.startswith('_OV') or l == 0:
            continue
        if s + l > core_end:
            die('area %s ends at 0x%04X beyond _DATA end 0x%04X' % (name, s + l, core_end))
    ovlens = {k: ar0['_OV%d' % k][1] for k in range(1, o.novl + 1)}
    maxlen = max(ovlens.values())

    # ---- 領域の番地を決め、(a)(b) を検査 ----
    ovlbase = (core_end + 15) & ~15
    imgtop = ((core_end + RESV + BLK - 1) // BLK) * BLK
    if ovlbase + maxlen > imgtop:
        die('overlay region 0x%04X + %d = 0x%04X exceeds imgtop 0x%04X '
            '(core %d B, short by %d B) / overlays: %s'
            % (ovlbase, maxlen, ovlbase + maxlen, imgtop, core_end,
               ovlbase + maxlen - imgtop,
               ' '.join('%d:%d' % (k, ovlens[k]) for k in range(1, o.novl + 1))))

    # ---- core ----
    imgc, arc = link(o, 'core', {}, ovlbase)
    core = bytearray(slice_img(imgc, 0, core_end))
    if arc['_DATA'] != ar0['_DATA']:
        die('core layout moved between links')

    # ---- 各オーバーレイ ----
    for k in range(1, o.novl + 1):
        imgk, ark = link(o, 'o%d' % k, {k: ovlbase}, ovlbase)
        if bytes(slice_img(imgk, 0, core_end)) != bytes(core):
            die('core bytes differ in overlay link %d (core must not depend on overlays)' % k)
        s, l = ark['_OV%d' % k]
        if s != ovlbase or l != ovlens[k]:
            die('overlay %d placed at 0x%04X/%d, expected 0x%04X/%d'
                % (k, s, l, ovlbase, ovlens[k]))
        with open('%s/%s%02d.ovl' % (o.dir, o.cmd, k), 'wb') as f:
            f.write(slice_img(imgk, ovlbase, ovlbase + l))

    # ---- 追加ブロックのヘッダ(Makefile の XBLK と同じ: 0x10 'T' 0x11 'Z' 0x12 n)----
    if o.xblk:
        core[0x10] = ord('T')
        core[0x11] = ord('Z')
        core[0x12] = o.xblk
    with open('%s/%s.bin' % (o.dir, o.cmd), 'wb') as f:
        f.write(core)

    sys.stderr.write(
        'ovllink.py: %s.bin %d B (image %d blk, imgtop 0x%04X) / region 0x%04X..0x%04X '
        '(max ovl %d B, spare %d B) / %d overlays: %s\n'
        % (o.cmd, core_end, imgtop // BLK, imgtop, ovlbase, ovlbase + maxlen,
           maxlen, imgtop - ovlbase - maxlen, o.novl,
           ' '.join('%d:%d' % (k, ovlens[k]) for k in range(1, o.novl + 1))))


if __name__ == '__main__':
    main()
