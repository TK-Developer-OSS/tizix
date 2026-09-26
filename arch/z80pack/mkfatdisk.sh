#!/bin/sh
# mkfatdisk.sh - tizix 用 FAT12 ディスク(driveb.dsk)
#
#   UNIX 風レイアウト:
#     /bin   … 全コマンド + DRIVER.BIN(launch は /bin/<CMD>.BIN を開く)
#     /root  … シェルの初期カレント(ホーム)
#     /dev   … 合成(ディスク上には無い。vfs.c が持つ)
#
#   ・driveb.dsk が無ければ: mkfs.fat して /bin /root を作り、全コマンドを配置。
#   ・driveb.dsk が有れば  : mkfs せず、/bin 以下のコマンドを mcopy -o で
#     上書きするだけ(実機の「FS は永続」に寄せる。テストが作ったファイルや
#     /root の中身はそのまま残る)。丸ごと作り直したいときは
#     `rm arch/z80pack/disks/driveb.dsk` してから make。
#
#   cpmsim フロッピー幾何: 77 track x 26 sect x 128B = 256256B。
#   FatFs は 512B セクタ(=4x128B)。FAT12 領域 500 sect、残りパディング。
set -e
cd "$(dirname "$0")/../.."          # repo ルート基準
DSK=arch/z80pack/disks/driveb.dsk
IMG=fatimg.raw
MTOOLSRC_FILE=/tmp/tizix_mtoolsrc

# /bin へ入れるコマンド: arch/z80pack/user/*.bin を全部そのまま配置する
#   (driver.bin 含む)。ビルド生成物は user/Makefile が arch/z80pack/user/ へ
#   出力する(user/ にはソースだけ)。
#   個別リストは持たない。足し忘れると「ビルドは通るがディスクに載らない」で
#   ハマる(#24 で新コマンドが載らず、旧 tail.bin の無限ループを踏んだ)。
#   user/Makefile の COMMANDS が唯一の真実。
#   sh は z80 で外部コマンド化。init(PID 1)が /bin/sh.bin をロード・respawn する。

export MTOOLS_SKIP_CHECK=1

# $1 = mtools が触るイメージへの絶対パス
# 名前は小文字で書く。mtools が all-lowercase 8.3 を「SFN + NT 小文字フラグ」で
# 書き(LFN 無し)、FatFs(ff.c パッチ)がそれを尊重して小文字で返す。
sync_bin_dir() {
	printf 'drive x: file="%s" exclusive\n' "$1" > "$MTOOLSRC_FILE"
	export MTOOLSRC="$MTOOLSRC_FILE"
	# -D s: 名前衝突(既存ディレクトリ)時は無言でスキップ。
	#   -D s が無いと mtools が /dev/tty から直接 y/n 確認を読みに行き、
	#   このスクリプトの 2>/dev/null でプロンプト文字だけ消えて見えなくなり、
	#   make 全体が理由不明のまま無限に固まる(arch/z80board 側で発覚した
	#   ハングと同一原因。z80pack 側も同じパターンなので合わせて直す)。
	mmd -D s x:bin  2>/dev/null || true  # 旧レイアウトのディスクでも自己修復
	mmd -D s x:root 2>/dev/null || true
	mmd -D s x:etc  2>/dev/null || true  # /etc/rc (起動スクリプト)
	mmd -D s x:var  2>/dev/null || true  # /var/log (rsyslog)
	mmd -D s x:var/log 2>/dev/null || true
	# *.ovl は関数単位オーバーレイ(#69、vi01.ovl 等)。本体が実行中に
	#   /bin/<cmd>NN.ovl を読みに来るので、本体と同じ場所に置く。
	for b in arch/z80pack/user/*.bin arch/z80pack/user/*.ovl; do
		[ -f "$b" ] || continue
		mcopy -o "$b" "x:bin/$(basename "$b")"
	done
	mcopy -o arch/z80pack/etc/rc "x:etc/rc"
}

if [ -f "$DSK" ]; then
	echo "driveb.dsk 既存 → /bin コマンドを上書きのみ(mkfs しない)"
	sync_bin_dir "$PWD/$DSK"
	mdir x:bin
	sync
	echo "updated $DSK"
	exit 0
fi

echo "driveb.dsk 新規作成(mkfs + /bin /root)"
dd if=/dev/zero of="$IMG" bs=512 count=500 status=none
# -s 1 = 1 クラスタ 1 セクタ(512B)。既定だと 250KB に対して 2KB クラスタに
# なり、1 バイトのファイルでも 2KB 食う。コマンドが 70 本を超えた時点で空きが
# 20KB まで減り、test_vi の小さなファイル十数個が書けずに空になっていた
# (2026-09-25、#71 の検証中に発見)。512B なら無駄はほぼ出ない。
mkfs.fat -F 12 -S 512 -s 1 -n TIZIX "$IMG" >/dev/null
sync_bin_dir "$PWD/$IMG"
echo "=== driveb 内容 ==="
mdir x:
mdir x:bin
mkdir -p arch/z80pack/disks
cp "$IMG" "$DSK"
truncate -s 256256 "$DSK"      # cpmsim フロッピーサイズへパディング
sync                          # ページキャッシュを確実にフラッシュ(\\rocky9 共有の
                             # write-back 遅延で cpmsim が途中書きを読むのを防ぐ)
rm -f "$IMG"
echo "created $DSK"
