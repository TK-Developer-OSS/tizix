#!/usr/bin/env python3
"""mkhex.py kernel.bin disk.img disk_offset out.hex
Lay out the kernel image from 0 and the disk image from disk_offset, and write 32-bit little-endian words,
one word per line in hex (for $readmemh)."""
import sys

kern = open(sys.argv[1], 'rb').read()
disk = open(sys.argv[2], 'rb').read()
off = int(sys.argv[3], 0)
if len(kern) > off:
    sys.exit("kernel (%d bytes) overlaps the disk offset 0x%x" % (len(kern), off))
img = bytearray(off + len(disk))
img[:len(kern)] = kern
img[off:] = disk
img += b'\0' * (-len(img) % 4)
with open(sys.argv[4], 'w') as f:
    for i in range(0, len(img), 4):
        f.write('%08x\n' % int.from_bytes(img[i:i+4], 'little'))
