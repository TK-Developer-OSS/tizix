#!/bin/sh
# run_dev_dd_check.sh - #32 の仕上げ確認。
#   test_dev_dd.py が cpmsim 内で作った .IMG を mtools で driveb.dsk から
#   吸い出し、**生イメージの該当オフセットとバイト比較**する。
#   ゲスト内では中身を突き合わせる手段が無いので、ここが本当の検証。
#
#   使い方: sh python/run_dev_dd_check.sh   (tizix のルートから)
set -e

TIZIX=${TIZIX_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}   # #21: 既定はこのスクリプトの位置から
DISKS="$TIZIX/arch/z80pack/disks"
TMP=$(mktemp -d)
MTRC="$TMP/mtoolsrc"

printf 'drive x: file="%s" exclusive\n' "$DISKS/driveb.dsk" > "$MTRC"
export MTOOLS_SKIP_CHECK=1
export MTOOLSRC="$MTRC"

rc=0

pull() {   # pull <guest name> <local name>
    mcopy -o -i "$DISKS/driveb.dsk" "x:root/$1" "$TMP/$2" 2>/dev/null \
        || mcopy -o "x:root/$1" "$TMP/$2"
}

cmp_range() {   # cmp_range <local file> <raw image> <skip bytes> <len> <label>
    dd if="$2" of="$TMP/ref.bin" bs=1 skip="$3" count="$4" status=none
    if cmp -s "$TMP/$1" "$TMP/ref.bin"; then
        echo "[OK ] $5"
    else
        echo "[FAIL] $5"
        rc=1
    fi
}

pull VBR.IMG  vbr.bin
pull BOOT.IMG boot.bin
pull A1.IMG   a1.bin

# drive B の 2 セクタ目(= FAT 本体)は照合しない。ゲストが .IMG を作った時点で
# FAT が更新されるので、読んだ後の driveb.dsk と一致しなくて当たり前。
# skip= の中身照合は、ゲストが書き換えない drive A 側で行う。
cmp_range vbr.bin  "$DISKS/driveb.dsk" 0    512  "/dev/fdb sector0 == driveb.dsk[0:512] (FAT12 VBR)"
cmp_range boot.bin "$DISKS/drivea.dsk" 0    512  "/dev/fda sector0 == drivea.dsk[0:512] (boot)"
cmp_range a1.bin   "$DISKS/drivea.dsk" 512  512  "/dev/fda skip=1  == drivea.dsk[512:1024]"

# VBR らしさの目視確認(TIZIX ラベル / FAT12 / 0x55AA)
if od -A d -c -j 43 -N 16 "$TMP/vbr.bin" | grep -q "T   I   Z   I   X"; then
    echo "[OK ] 取り出した VBR に TIZIX ラベルが入っている"
else
    echo "[FAIL] VBR のラベルが読めない"
    rc=1
fi

rm -rf "$TMP"
echo "=== dev_dd host check: $([ $rc -eq 0 ] && echo PASS || echo FAIL) ==="
exit $rc
