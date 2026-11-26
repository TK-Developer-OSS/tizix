#!/bin/sh
# mkfatdisk.sh - the CPC 6128 disk's FAT12 volume (359 sectors of 512B: the 360 of a 180KB disk less the boot sector).
# Its reserved sectors hold the kernel image and hi.s (mkdisk.py puts them there); the boot sector and the DSK wrapper
# are made by mkdisk.py.  The FAT part is made by ../../common-z80/mkfatdisk.sh.
set -e
ARCH_DIR="$(cd "$(dirname "$0")" && pwd)"
TZ_ARCH=z80-amstradcpc
TZ_DSK=arch/z80-amstradcpc/cpc6128/disks/fat.img
TZ_NSECT=359
TZ_MKFS_OPTS="-s 1 -R 80 -r 64 -f 2 -M 0xFC -g 1/9 -h 0"
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT=
mkdir -p "$ARCH_DIR/disks"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$ARCH_DIR/../../common-z80/mkfatdisk.sh"
