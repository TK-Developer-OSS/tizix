#!/usr/bin/env python3
"""mkdisk.py BOOT.IHX KERNEL.BIN FAT.IMG DISK : build the RC2014 Compact Flash image (raw, 512B sectors).
  The card is the FAT12 volume FAT.IMG from sector 0; its reserved sectors hold
    sector 2: RomWBW's boot info (signature 0xA55A, load address 0xA000, end 0xA200, entry 0xA000, label),
    sector 3: boot.s (loaded to 0xA000 by RomWBW),
    sectors 4-67: the kernel image (boot.s KFIRST = 4, KSECT = 64; loaded to 0x0100).
  The BPB that mkfs.fat wrote stays as it is."""
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools"))
from ihx2inc import read_ihx

KFIRST, KSECT, BOOT = 4, 64, 0xA000
boot_ihx, kernel_bin, fat_img, out = sys.argv[1:5]
bm = read_ihx(boot_ihx)
kernel = open(kernel_bin, "rb").read()
img = bytearray(open(fat_img, "rb").read())
if struct.unpack_from("<H", img, 0x0B)[0] != 512: sys.exit("not 512 bytes per sector")
reserved = struct.unpack_from("<H", img, 0x0E)[0]
if reserved < KFIRST + KSECT: sys.exit("reserved sectors %d too few" % reserved)
if len(kernel) > KSECT * 512: sys.exit("kernel is over %d bytes" % (KSECT * 512))

info = bytearray(512)
struct.pack_into("<H", info, 0x180, 0xA55A)            # BB_SIG
info[0x1E7:0x1F7] = b"tizix           "                # BB_LABEL, 16 characters
info[0x1F7] = ord("$")                                 # BB_TERM
struct.pack_into("<HHHH", info, 0x1F8, 0, BOOT, BOOT + 512, BOOT)   # BB_BILOC, BB_CPMLOC, BB_CPMEND, BB_CPMENT
img[2 * 512:3 * 512] = info

boot = bytearray(512)
for a, v in bm.items():
    if not BOOT <= a < BOOT + 512: sys.exit("boot byte at %04x outside the sector" % a)
    boot[a - BOOT] = v
img[3 * 512:4 * 512] = boot
img[KFIRST * 512:KFIRST * 512 + len(kernel)] = kernel

open(out, "wb").write(img)
print("mkdisk: boot %d bytes, kernel %d bytes, volume %d sectors -> %s" % (max(bm) - BOOT + 1, len(kernel), len(img) // 512, out))
