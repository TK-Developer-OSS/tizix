#!/usr/bin/env python3
"""mkdisk.py BOOT.IHX KERNEL.BIN FAT.IMG DISK : build the TRS-80 4P disk (a JV3 image, 256B double density sectors).
  Sector n (0-based, 18 per track, ids 0..17): n=0 unused, n=1 the boot sector (the 4P boot ROM reads id 1 of track 0),
  n>=2 the FAT12 volume FAT.IMG (FatFs sector s = n 2+2s, 2+2s+1) with the kernel image laid in its reserved sectors
  from FatFs sector 2 (boot.s KFIRST = 6).  The BPB that mkfs.fat wrote stays as it is."""
import struct, sys
from ihx2inc import read_ihx

boot_ihx, kernel_bin, fat_img, out = sys.argv[1:5]
mem = read_ihx(boot_ihx)
kernel = open(kernel_bin, "rb").read()
fat = bytearray(open(fat_img, "rb").read())
reserved = struct.unpack_from("<H", fat, 0x0E)[0]
if struct.unpack_from("<H", fat, 0x0B)[0] != 512: sys.exit("not 512 bytes per sector")
if reserved < 2 + (len(kernel) + 511) // 512: sys.exit("reserved sectors %d too few for the kernel" % reserved)
if len(kernel) > 127 * 256: sys.exit("kernel is over 127 sectors")
fat[1024:1024 + len(kernel)] = kernel

boot = bytearray(256)
for a in range(0x4300, 0x4320):
    if a in mem: boot[a - 0x4300] = mem[a]
bstart = min(a for a in mem if a >= 0xF000)
bend = max(mem)
if bend - 0xF000 >= 0xE0: sys.exit("stage B is %d bytes, over 224" % (bend - 0xF000 + 1))
for a in range(0xF000, bend + 1): boot[0x20 + a - 0xF000] = mem.get(a, 0)

sectors = [bytes(256), bytes(boot)]
for i in range(0, len(fat), 256): sectors.append(bytes(fat[i:i + 256]))
if len(sectors) > 2901: sys.exit("%d sectors do not fit JV3 (2901)" % len(sectors))
hdr = b""
for n in range(len(sectors)):
    hdr += bytes([n // 18, n % 18, 0x80])      # track, id, flags: 0x80 = double density, size code 0 = 256
hdr += bytes([0xFF, 0xFF, 0xFC]) * (2901 - len(sectors))
open(out, "wb").write(hdr + b"\xff" + b"".join(sectors))
print("mkdisk: %d sectors (%d tracks), kernel %d bytes, boot stage B %d bytes -> %s" % (len(sectors), (len(sectors) + 17) // 18, len(kernel), bend - 0xF000 + 1, out))
