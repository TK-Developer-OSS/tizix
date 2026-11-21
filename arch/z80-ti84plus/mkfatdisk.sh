#!/bin/sh
# mkfatdisk.sh - the FAT12 image that goes into the flash of the TI-84 Plus (pages 2.., read-only for now).
#   960KB = 60 pages x 16KB. The common recipe lives in ../common-z80/mkfatdisk.sh.
TZ_ARCH=z80-ti84plus
TZ_DSK=arch/z80-ti84plus/flashdisk.img
TZ_NSECT=1920
TZ_MKFS_OPTS=
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT="the image is appended to the ROM by make (rom.bin = kernel + disk)"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$(dirname "$0")/../common-z80/mkfatdisk.sh"
