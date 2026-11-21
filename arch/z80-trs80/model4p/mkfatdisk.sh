#!/bin/sh
# mkfatdisk.sh - the TRS-80 4P disk's FAT12 volume (720KB).  Its reserved sectors hold the kernel image (mkdisk.py puts it
# there); the JV3 wrapper and the boot sector are made by mkdisk.py.  The FAT part is made by ../../common-z80/mkfatdisk.sh.
set -e
ARCH_DIR="$(cd "$(dirname "$0")" && pwd)"
TZ_ARCH=z80-trs80
TZ_DSK=arch/z80-trs80/model4p/disks/fat.img
TZ_NSECT=1440
TZ_MKFS_OPTS="-s 2 -R 66 -r 112 -f 2 -M 0xF9 -g 2/9 -h 0"
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT=
mkdir -p "$ARCH_DIR/disks"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$ARCH_DIR/../../common-z80/mkfatdisk.sh"
