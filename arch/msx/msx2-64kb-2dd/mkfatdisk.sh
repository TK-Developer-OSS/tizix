#!/bin/sh
# mkfatdisk.sh - the MSX2 2DD disk: one FAT12 volume (720KB) whose reserved sectors hold the
# boot sector, s2 and the kernel image.  The FAT part is made by ../../common-z80/mkfatdisk.sh; the boot code and the
# kernel are laid over it by mkdisk.py every time (the common script keeps an existing image).
set -e
ARCH_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$ARCH_DIR/../../.." && pwd)"

TZ_ARCH=msx
TZ_DSK=arch/msx/msx2-64kb-2dd/disks/tizix.dsk
TZ_NSECT=1440                                   # 80 tracks x 2 sides x 9 sectors
TZ_MKFS_OPTS="-s 2 -R 66 -r 112 -f 2 -M 0xF9 -g 2/9 -h 0"
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT=
mkdir -p "$ARCH_DIR/disks"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
sh "$ARCH_DIR/../../common-z80/mkfatdisk.sh"

OBJ="$ROOT/build/arch/msx/obj"
python3 "$ARCH_DIR/mkdisk.py" "$OBJ/boot.ihx" "$OBJ/s2.ihx" "$OBJ/kernel.bin" "$ROOT/$TZ_DSK"
