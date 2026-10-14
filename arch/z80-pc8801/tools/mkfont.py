import struct, sys
fon = r"C:\Windows\Fonts\cga80woa.fon"
d = open(fon, "rb").read()
ne = struct.unpack_from("<I", d, 0x3c)[0]
rt = ne + struct.unpack_from("<H", d, ne + 0x24)[0]
shift = struct.unpack_from("<H", d, rt)[0]
p = rt + 2
fnts = []
while True:
    tid = struct.unpack_from("<H", d, p)[0]
    if tid == 0: break
    cnt = struct.unpack_from("<H", d, p + 2)[0]; p += 8
    for _ in range(cnt):
        off, ln = struct.unpack_from("<HH", d, p); p += 12
        if tid == 0x8008:
            fnts.append(d[off << shift:(off << shift) + (ln << shift)])
print("FNT resources:", len(fnts))
best = None
for f in fnts:
    ver = struct.unpack_from("<H", f, 0)[0]
    w, h = struct.unpack_from("<HH", f, 86)
    first, last = f[95], f[96]
    print("ver %04x %dx%d chars %d-%d" % (ver, w, h, first, last))
    if h == 8 and w == 8: best = (f, ver, first, last)
if not best: sys.exit("8x8 が無い")
f, ver, first, last = best
tab = 118 if ver < 0x300 else 148
step = 4 if ver < 0x300 else 6
rom = bytearray(256 * 8)
for c in range(first, last + 1):
    e = tab + (c - first) * step
    if ver < 0x300:
        width, off = struct.unpack_from("<HH", f, e)
    else:
        width, off = struct.unpack_from("<HI", f, e)
    for r in range(8):
        rom[c * 8 + r] = f[off + r]      # 幅 8 なら 1 ストリップ = 高さ 8 バイト(行順)
open(r"D:\Temp\FONT_CGA.ROM", "wb").write(rom)
for ch in "Agy@/":
    o = ord(ch)
    for r in range(8): print("".join("#" if rom[o*8+r] & (0x80 >> x) else "." for x in range(8)))
    print()
