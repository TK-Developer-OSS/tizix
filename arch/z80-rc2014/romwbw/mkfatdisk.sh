#!/bin/sh
# mkfatdisk.sh - the FAT12 volume of the RC2014 Compact Flash image (4MB, 8192 sectors of 512B, 1KB clusters).  The volume
# starts at the first sector of the card; its reserved sectors hold RomWBW's boot info sector, boot.s and the kernel
# (mkdisk.py puts them there).  The FAT part is made by ../../common-z80/mkfatdisk.sh.
set -e
ARCH_DIR="$(cd "$(dirname "$0")" && pwd)"
TZ_ARCH=z80-rc2014
TZ_DSK=arch/z80-rc2014/romwbw/disks/fat.img
TZ_NSECT=8192
TZ_MKFS_OPTS="-s 2 -R 72 -r 256 -f 2 -M 0xF8 -h 0"
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT=
mkdir -p "$ARCH_DIR/disks"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$ARCH_DIR/../../common-z80/mkfatdisk.sh"
