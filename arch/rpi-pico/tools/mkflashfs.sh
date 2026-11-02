#!/bin/sh
# tools/mkflashfs.sh - 内蔵 Flash に抱かせる FAT イメージ(SD が無いときのドライブ 0)を作る
#   使い方: mkflashfs.sh OUT.img SIZE_KB ARCH_DIR
#   中身は SD の sdcard.img と同じ並び(/bin /etc/rc /root /var/log /usr/share/tzsh)。
#   /bin には ARCH_DIR/build/obj/*.bin があれば入れる(カーネルのビルドはコマンドに依存しない)。
set -e
OUT=$1; KB=$2; A=$3
rm -f "$OUT"
mkfs.fat -F 12 -S 512 -n TIZIXFLASH -C "$OUT" "$KB" >/dev/null
for d in root bin etc var var/log usr usr/share usr/share/tzsh tmp; do mmd -i "$OUT" ::$d; done
mcopy -o -i "$OUT" "$A/etc/rc" ::etc/rc
for b in "$A"/build/obj/*.bin; do
  [ -f "$b" ] && mcopy -o -i "$OUT" "$b" ::bin/"$(basename "$b")"
done
for s in "$A"/../../user/tzsh/*.sh; do
  [ -f "$s" ] && mcopy -o -i "$OUT" "$s" ::usr/share/tzsh/
done
exit 0
