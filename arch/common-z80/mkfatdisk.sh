#!/bin/sh
# arch/common-z80/mkfatdisk.sh - tizix 用 FAT12 ディスクを作る / 更新する(z80pack・z80board 共通)
#
#   直接は呼ばない。arch/<arch>/mkfatdisk.sh が寸法を環境変数に入れて呼ぶ
#   (ボードごとに違うのは「どこに、どの大きさで、どんな形で置くか」だけ):
#     TZ_ARCH        アーキ名(build/arch/<arch>/… と arch/<arch>/etc/rc の場所を決める)
#     TZ_DSK         できあがるイメージ(リポジトリのルートからの相対パス)
#     TZ_NSECT       FAT 領域の大きさ(512B セクタ数)
#     TZ_MKFS_OPTS   mkfs.fat への追加指定(無ければ空)
#     TZ_PAD_BYTES   できたイメージをこの大きさまで伸ばす(要らなければ空)
#     TZ_MTOOLS_OPTS mtoolsrc の drive 行への追加指定(無ければ空)
#     TZ_DSK_HINT    作り終えたときに添える一言(無ければ空)
#
#   UNIX 風レイアウト:
#     /bin      … 全コマンド + DRIVER.BIN(launch は /bin/<CMD>.BIN を開く)
#     /root     … シェルの初期カレント(ホーム)
#     /etc      … /etc/rc(sh が起動時に 1 度だけ実行する起動スクリプト)
#     /var/log  … klog_write / rsyslog の出力先(/var/log/message)
#     /dev      … 合成(ディスク上には無い。vfs.c が持つ)
#
#   ・イメージが無ければ: mkfs.fat してディレクトリを作り、全コマンドを配置。
#   ・イメージが有れば  : mkfs せず、/bin 以下のコマンドを mcopy -o で上書きするだけ
#     (実機の「FS は永続」に寄せる。テストが作ったファイルや /root の中身はそのまま残る)。
#     丸ごと作り直したいときはイメージを消してから make(= make cleandisk)。
set -e
: "${TZ_ARCH:?arch/<arch>/mkfatdisk.sh から呼ぶこと}" "${TZ_DSK:?}" "${TZ_NSECT:?}"
cd "$(dirname "$0")/../.."          # repo ルート基準
DSK=$TZ_DSK
NAME=$(basename "$DSK")
OBJ=build/arch/$TZ_ARCH/obj
USERBIN=build/arch/$TZ_ARCH/user
# mtoolsrc と作りかけのイメージは中間物として build/ へ(/tmp の固定名は別ユーザーの
# 残骸で Permission denied になった)。
mkdir -p "$OBJ"
MTOOLSRC_FILE=$PWD/$OBJ/mtoolsrc
IMG=$OBJ/fatimg.raw

# /bin へ入れるコマンド: build/arch/<arch>/user/*.bin を全部そのまま配置する
#   (driver.bin 含む)。ビルド生成物は user/Makefile と tzcc が build/arch/<arch>/user/ へ
#   出力する(user/ にはソースだけ)。
#   個別リストは持たない。足し忘れると「ビルドは通るがディスクに載らない」で
#   ハマる(#24 で新コマンドが載らず、旧 tail.bin の無限ループを踏んだ)。
#   sh は z80 で外部コマンド化。init(PID 1)が /bin/sh.bin をロード・respawn する。

export MTOOLS_SKIP_CHECK=1

# $1 = mtools が触るイメージへの絶対パス
# 名前は小文字で書く。mtools が all-lowercase 8.3 を「SFN + NT 小文字フラグ」で
# 書き(LFN 無し)、FatFs(ff.c パッチ)がそれを尊重して小文字で返す。
sync_bin_dir() {
	printf 'drive x: file="%s"%s\n' "$1" "${TZ_MTOOLS_OPTS:+ $TZ_MTOOLS_OPTS}" > "$MTOOLSRC_FILE"
	export MTOOLSRC="$MTOOLSRC_FILE"
	# -D s: 名前衝突(既存ディレクトリ)時は無言でスキップ。
	#   -D s が無いと mtools が /dev/tty から直接 y/n 確認を読みに行き、
	#   このスクリプトの 2>/dev/null でプロンプト文字だけ消えて見えなくなり、
	#   make 全体が理由不明のまま無限に固まる(z80board で発覚したハングの原因)。
	mmd -D s x:bin  2>/dev/null || true  # 旧レイアウトのディスクでも自己修復
	mmd -D s x:root 2>/dev/null || true
	mmd -D s x:etc  2>/dev/null || true  # /etc/rc (起動スクリプト)
	mmd -D s x:var  2>/dev/null || true  # /var/log (rsyslog / klog_write)
	mmd -D s x:var/log 2>/dev/null || true
	# *.ovl は関数単位オーバーレイ(#69、vi01.ovl 等)。本体が実行中に
	#   /bin/<cmd>NN.ovl を読みに来るので、本体と同じ場所に置く。
	for b in "$USERBIN"/*.bin "$USERBIN"/*.ovl; do
		[ -f "$b" ] || continue
		mcopy -o "$b" "x:bin/$(basename "$b")"
	done
	mcopy -o "arch/$TZ_ARCH/etc/rc" "x:etc/rc"
}

if [ -f "$DSK" ]; then
	echo "$NAME 既存 → /bin コマンドを上書きのみ(mkfs しない)"
	sync_bin_dir "$PWD/$DSK"
	mdir x:bin
	sync
	echo "updated $DSK"
	exit 0
fi

echo "$NAME 新規作成(mkfs + /bin /root)"
dd if=/dev/zero of="$IMG" bs=512 count="$TZ_NSECT" status=none
# shellcheck disable=SC2086  # TZ_MKFS_OPTS は空白で区切った複数の指定
mkfs.fat -F 12 -S 512 $TZ_MKFS_OPTS -n TIZIX "$IMG" >/dev/null
sync_bin_dir "$PWD/$IMG"
echo "=== $NAME 内容 ==="
mdir x:
mdir x:bin
mkdir -p "$(dirname "$DSK")"
cp "$IMG" "$DSK"
if [ -n "${TZ_PAD_BYTES:-}" ]; then
	truncate -s "$TZ_PAD_BYTES" "$DSK"
fi
sync                          # ページキャッシュを確実にフラッシュ(\\rocky9 共有の
                             # write-back 遅延でシミュレータが途中書きを読むのを防ぐ)
rm -f "$IMG"
echo "created $DSK${TZ_DSK_HINT:+ ($TZ_DSK_HINT)}"
