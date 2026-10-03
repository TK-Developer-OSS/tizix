#!/bin/sh
# mkfatdisk.sh - z80board の FAT12 ディスク(sdcard.img。実機では物理 SD カードになる)
#
#   作り方は z80pack と共通で ../common-z80/mkfatdisk.sh にある。ここにあるのは
#   このボードに依る寸法だけ:
#     実機はこのイメージがフロッピーではなく SD カードに置き換わる。cpmsim の
#     フロッピー幾何(256256B)へのパディングは行わない。生の FAT12 イメージ
#     (パーティションテーブル無しの superfloppy 形式)なので、Rufus の「DD イメージ」
#     書き込みでそのまま SD カードに焼ける。
#   丸ごと作り直したいときは `rm arch/z80board/sdcard.img` してから make
#   (= make cleandisk)。
TZ_ARCH=z80board
TZ_DSK=arch/z80board/sdcard.img
# SD カードイメージのサイズ(セクタ数、512B/sector)。実機は SD カードそのもの
# なので floppy のような上限は無い。当面 8192 セクタ = 4MB(vi/coreutils の
# 育ちに余裕を持たせる)。
TZ_NSECT=8192
TZ_MKFS_OPTS=
TZ_PAD_BYTES=
TZ_MTOOLS_OPTS=
TZ_DSK_HINT="Rufus の「DD イメージ」で SD カードへ、または: dd if=arch/z80board/sdcard.img of=/dev/sdX bs=512"
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
exec sh "$(dirname "$0")/../common-z80/mkfatdisk.sh"
