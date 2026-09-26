#!/bin/sh
# mkfatdisk.sh - tizix 用 FAT12 ディスク(sdcard.img, z80board 実機版)
#
#   z80pack 版(arch/z80pack/mkfatdisk.sh)とほぼ同じだが、実機はこのイメージが
#   フロッピーではなく物理 SD カードに置き換わる(Rufus 等で dd 相当書き込み)。
#   したがって cpmsim フロッピー幾何(256256B)へのパディングは行わない。
#   生の FAT12 イメージ(パーティションテーブル無しの superfloppy 形式)なので
#   Rufus の「DD イメージ」書き込みでそのまま SD カードに焼ける。
#
#   UNIX 風レイアウト:
#     /bin   … 全コマンド + DRIVER.BIN(launch は /bin/<CMD>.BIN を開く)
#     /root  … シェルの初期カレント(ホーム)
#     /etc   … /etc/rc (sh が起動時に 1 度だけ実行する起動スクリプト)
#     /var/log … klog_write / rsyslog の出力先 (/var/log/message)
#     /dev   … 合成(ディスク上には無い。vfs.c が持つ)
#
#   ・sdcard.img が無ければ: mkfs.fat して /bin /root を作り、全コマンドを配置。
#   ・sdcard.img が有れば  : mkfs せず、/bin 以下のコマンドを mcopy -o で
#     上書きするだけ(実機の「FS は永続」に寄せる)。丸ごと作り直したいときは
#     `rm arch/z80board/sdcard.img` してから make。
set -e
cd "$(dirname "$0")/../.."          # repo ルート基準
DSK=arch/z80board/sdcard.img
IMG=fatimg_z80board.raw
MTOOLSRC_FILE=/tmp/tizix_z80board_mtoolsrc

# SD カードイメージのサイズ(セクタ数、512B/sector)。実機は SD カードそのもの
# なので floppy のような上限は無い。当面 8192 セクタ = 4MB(vi/coreutils の
# 育ちに余裕を持たせる)。Rufus の「DD イメージ」書き込み、または
# dd でこの生イメージをそのまま SD カードへ書けばよい。
NSECT=8192

# /bin へ入れるコマンド: arch/z80board/user/*.bin を全部そのまま配置する
#   (driver.bin 含む)。ビルド生成物は user/Makefile(ARCH=z80board)が
#   arch/z80board/user/ へ出力する(user/ にはソースだけ)。
#   個別リストは持たない(z80pack 版と同じ理由。#24 の教訓)。
#   sh は z80 で外部コマンド化。init(PID 1)が /bin/sh.bin をロード・respawn する。

export MTOOLS_SKIP_CHECK=1

# $1 = mtools が触るイメージへの絶対パス
sync_bin_dir() {
	printf 'drive x: file="%s"\n' "$1" > "$MTOOLSRC_FILE"
	export MTOOLSRC="$MTOOLSRC_FILE"
	# -D s: 名前衝突(既存ディレクトリ)時は無言でスキップ。
	#   -D s が無いと mtools が /dev/tty から直接 y/n 確認を読みに行き、
	#   このスクリプトの 2>/dev/null でプロンプト文字だけ消えて見えなくなり、
	#   make 全体が理由不明のまま無限に固まる(#過去のハングの原因)。
	mmd -D s x:bin  2>/dev/null || true  # 旧レイアウトのディスクでも自己修復
	mmd -D s x:root 2>/dev/null || true
	mmd -D s x:etc  2>/dev/null || true  # /etc/rc (起動スクリプト)
	mmd -D s x:var  2>/dev/null || true  # /var/log (rsyslog / klog_write)
	mmd -D s x:var/log 2>/dev/null || true
	# *.ovl は関数単位オーバーレイ(#69、vi01.ovl 等)。本体と同じ /bin へ。
	for b in arch/z80board/user/*.bin arch/z80board/user/*.ovl; do
		[ -f "$b" ] || continue
		mcopy -o "$b" "x:bin/$(basename "$b")"
	done
	mcopy -o arch/z80board/etc/rc "x:etc/rc"
}

if [ -f "$DSK" ]; then
	echo "sdcard.img 既存 → /bin コマンドを上書きのみ(mkfs しない)"
	sync_bin_dir "$PWD/$DSK"
	mdir x:bin
	sync
	echo "updated $DSK"
	exit 0
fi

echo "sdcard.img 新規作成(mkfs + /bin /root, z80board 実機用 SD イメージ)"
dd if=/dev/zero of="$IMG" bs=512 count=$NSECT status=none
mkfs.fat -F 12 -S 512 -n TIZIX "$IMG" >/dev/null
sync_bin_dir "$PWD/$IMG"
echo "=== sdcard.img 内容 ==="
mdir x:
mdir x:bin
cp "$IMG" "$DSK"
sync                          # ページキャッシュを確実にフラッシュ(\\rocky9 共有の
                             # write-back 遅延対策。z80pack 版と同じ理由)
rm -f "$IMG"
echo "created $DSK (Rufus の「DD イメージ」で SD カードへ、または: dd if=$DSK of=/dev/sdX bs=512)"
