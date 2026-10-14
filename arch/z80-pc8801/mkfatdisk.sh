#!/bin/sh
set -e
ARCH_DIR="$(cd "$(dirname "$0")" && pwd)"

TZ_ARCH=z80-pc8801
# 共通スクリプトの約束: リポジトリルートからの相対パス
TZ_DSK=arch/z80-pc8801/disks/driveb.raw
TZ_NSECT=500                  # cpmsim 幾何(77x26x128=256256B)に収まる範囲
TZ_MKFS_OPTS="-s 1"
TZ_PAD_BYTES=327680           # PC-8801 2D フロッピーサイズ (320KB)
TZ_MTOOLS_OPTS=exclusive
TZ_DSK_HINT=

mkdir -p "$ARCH_DIR/disks"

# 共通スクリプトで生 FAT12 イメージ (driveb.raw) を生成
export TZ_ARCH TZ_DSK TZ_NSECT TZ_MKFS_OPTS TZ_PAD_BYTES TZ_MTOOLS_OPTS TZ_DSK_HINT
sh "$ARCH_DIR/../common-z80/mkfatdisk.sh"

# 生 FAT12 イメージから PC-8801 用 D88 形式に変換
python3 "$ARCH_DIR/make_d88.py" "$ARCH_DIR/disks/driveb.raw" "$ARCH_DIR/disks/driveb.d88" "TIZIX DRIVE B"

# 回帰用: cpmsim は raw(.dsk)を読む。D88 は実機/エミュレータ用の包み。
cp "$ARCH_DIR/disks/driveb.raw" "$ARCH_DIR/disks/driveb.dsk"
