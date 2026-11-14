#!/usr/bin/env python3
"""mkdisk.py BOOT.IHX S2.IHX KERNEL.BIN DISK : put the MSX boot code, s2 and the kernel image into a FAT12 image made by
mkfs.fat.  The BPB (0x0B-0x1D) and the extended BPB with "FAT12" at 0x36 stay as mkfs.fat wrote them; the boot code goes to
0x00-0x02, 0x1E-0x20 and 0x3E-0xFE (only the first 256 bytes of the boot sector survive on a real disk ROM); s2 goes to
logical sector 1; the kernel image goes to logical sector 2 on (the reserved sectors)."""
import struct, sys
from ihx2inc import read_ihx

boot_ihx, s2_ihx, kernel_bin, disk = sys.argv[1:5]
mem = read_ihx(boot_ihx)
s2 = read_ihx(s2_ihx)
kernel = open(kernel_bin, "rb").read()
img = bytearray(open(disk, "rb").read())
reserved = struct.unpack_from("<H", img, 0x0E)[0]
if struct.unpack_from("<H", img, 0x0B)[0] != 512: sys.exit("not 512 bytes per sector")
need = (len(kernel) + 511) // 512
if reserved < 2 + need: sys.exit("reserved sectors %d < %d" % (reserved, 2 + need))
if max(mem) - 0xC000 >= 0xFF: sys.exit("boot code goes past 0xC0FE: only the first 256 bytes of the sector are safe")
if min(s2) != 0x8000 or max(s2) - 0x8000 >= 512: sys.exit("s2 must start at 0x8000 and fit in one sector")
get = lambda a, n: bytes(mem.get(0xC000 + a + i, 0) for i in range(n))
img[0:3] = get(0, 3)
img[0x1E:0x21] = get(0x1E, 3)
img[0x3E:0xFF] = get(0x3E, 0xC1)
img[512:1024] = bytes(s2.get(0x8000 + i, 0) for i in range(512))
img[1024:1024 + len(kernel)] = kernel
open(disk, "wb").write(img)
print("mkdisk: boot code, s2 (%d bytes) and %d bytes of kernel (%d sectors) put into %s" % (len(s2), len(kernel), need, disk))
