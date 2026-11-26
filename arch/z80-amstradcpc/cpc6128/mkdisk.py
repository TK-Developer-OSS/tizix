#!/usr/bin/env python3
"""mkdisk.py BOOT.IHX HI.IHX KERNEL.BIN FAT.IMG DISK : build the Amstrad CPC 6128 disk (a "MV - CPC" DSK in "system
format": 40 tracks, one side, 9 x 512B sectors, ids 0x41-0x49; physical sector p = track p / 9, id 0x41 + p % 9).
  p = 0: the boot sector (|CPM reads it to 0x0100; boot.s is assembled for 0x9000 and moves itself there),
  p >= 1: the FAT12 volume FAT.IMG (FatFs sector s = p - 1) with the kernel image laid in its reserved sectors from FatFs
  sector 1 (boot.s KFIRST = 2, KSECT = 64) and hi.s right after it (HSECT = 8).  The BPB that mkfs.fat wrote stays as it is."""
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
from ihx2inc import read_ihx
import dsk

KSECT, HSECT, BOOT = 64, 8, 0x9000
boot_ihx, hi_ihx, kernel_bin, fat_img, out = sys.argv[1:6]
bm = read_ihx(boot_ihx)
hm = read_ihx(hi_ihx)
kernel = open(kernel_bin, "rb").read()
fat = bytearray(open(fat_img, "rb").read())
if struct.unpack_from("<H", fat, 0x0B)[0] != 512: sys.exit("not 512 bytes per sector")
reserved = struct.unpack_from("<H", fat, 0x0E)[0]
if reserved < 1 + KSECT + HSECT: sys.exit("reserved sectors %d too few" % reserved)
if len(kernel) > KSECT * 512 - 256: sys.exit("kernel is over %d bytes" % (KSECT * 512 - 256))
hi = bytearray(HSECT * 512)
for a, v in hm.items():
    if not 0xF000 <= a < 0xF000 + len(hi): sys.exit("hi.s byte at %04x outside 0xF000-0x%X" % (a, 0xF000 + len(hi) - 1))
    hi[a - 0xF000] = v
fat[512:512 + len(kernel)] = kernel
fat[512 + KSECT * 512:512 + (KSECT + HSECT) * 512] = hi

boot = bytearray(512)
for a, v in bm.items():
    if not BOOT <= a < BOOT + 512: sys.exit("boot byte at %04x outside the sector" % a)
    boot[a - BOOT] = v

img = bytes(boot) + bytes(fat)
img += bytes(dsk.TRACKS * dsk.SPT * dsk.SSZ - len(img))
dsk.write_dsk(out, img)
print("mkdisk: kernel %d bytes, hi %d bytes, boot %d bytes, volume %d sectors -> %s"
      % (len(kernel), max(hm) - 0xF000 + 1, max(bm) - BOOT + 1, len(fat) // 512, out))
