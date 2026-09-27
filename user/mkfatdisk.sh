#!/bin/sh
# mkfatdisk.sh - tizix 用 FAT12 ディスク(driveb.dsk)を作り、コマンドを入れる
#   cpmsim フロッピー幾何: 77 track x 26 sect x 128B = 256256B
#   FatFs は 512B セクタ(=4x128B)。FAT12 領域は 500 sect(256000B)、残りはパディング。
#   ホストからは mtools でメンテ(mount 不要)。
set -e
IMG=fatimg.raw
DSK=disks/driveb.dsk
dd if=/dev/zero of="$IMG" bs=512 count=500 status=none
mkfs.fat -F 12 -S 512 -n TIZIX "$IMG" >/dev/null
export MTOOLS_SKIP_CHECK=1
mkdir -p build
cat > build/mtoolsrc <<RC
drive x: file="$PWD/$IMG" exclusive
RC
export MTOOLSRC="$PWD/build/mtoolsrc"
# 入れたいコマンドをここに追加
mcopy -o user/hello.bin  x:HELLO.BIN
mcopy -o user/a.bin      x:A.BIN
mcopy -o user/b.bin      x:B.BIN
mcopy -o user/driver.bin x:DRIVER.BIN    # block1 常駐 I/O ドライバ(起動時ロード必須)
mcopy -o user/date.bin   x:DATE.BIN      # Unix タイムスタンプ表示
mcopy -o user/date_dbg.bin x:DATEDBG.BIN # date デバッグ版(内部 call/jp 無し・実機切り分け用)
mcopy -o user/test1.bin  x:TEST1.BIN     # 1..10 と合計表示(手作業コード)
mdir x:
mkdir -p disks
cp "$IMG" "$DSK"
truncate -s 256256 "$DSK"      # cpmsim フロッピーサイズへパディング
echo "created $DSK"
