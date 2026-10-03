#!/bin/sh
# mkfatdisk.sh - z80pack の FAT12 ディスク(cpmsim の drive B = disks/driveb.dsk)
#
#   作り方は z80board と共通で ../common-z80/mkfatdisk.sh にある。ここにあるのは
#   cpmsim のフロッピーに依る寸法だけ:
#     cpmsim フロッピー幾何: 77 track x 26 sect x 128B = 256256B。
#     FatFs は 512B セクタ(=4x128B)。FAT12 領域 500 sect、残りはパディング。
#   丸ごと作り直したいときは `rm arch/z80pack/disks/driveb.dsk` してから make
#   (= make cleandisk)。
TZ_ARCH=z80pack
TZ_DSK=arch/z80pack/disks/driveb.dsk
TZ_NSECT=500
# -s 1 = 1 クラスタ 1 セクタ(512B)。既定だと 250KB に対して 2KB クラスタに
# なり、1 バイトのファイルでも 2KB 食う。コマンドが 70 本を超えた時点で空きが
# 20KB まで減り、test_vi の小さなファイル十数個が書けずに空になっていた
# (2026-09-25、#71 の検証中に発見)。512B なら無駄はほぼ出ない。
TZ_MKFS_OPTS="-s 1"
TZ_PAD_BYTES=256256           # cpmsim フロッピーサイズへパディング
TZ_MTOOLS_OPTS=exclusive
TZ_DSK_HINT=
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$(dirname "$0")/../common-z80/mkfatdisk.sh"
