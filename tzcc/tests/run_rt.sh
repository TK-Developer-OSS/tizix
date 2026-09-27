#!/bin/bash
# run_rt.sh - tests/ok_*.c を cpmsim ベアメタルで実行し、実出力を期待値と比較する。
#
# テストファイル内の注記:
#   // EXPECT: <行>        期待出力の1行(複数書けば複数行)
#   // EXPECT-EMPTY        出力が無いことを期待
#   // RUN: skip <理由>    このテストは実機実行しない
#   注記が無いテストは "unspecified" として集計だけする。
#
# 使い方:  tests/run_rt.sh            全 ok_*.c
#          tests/run_rt.sh ok_18 ok_20  指定分のみ
set -u
cd "$(dirname "$0")/.."
BUILD=../build/tzcc/z80pack
# SDCC は SDCC_BIN(tzcc/Makefile と同じ変数)か、無ければ PATH の sdasz80 の場所(#21)。
SDCC=${SDCC_BIN:-$(dirname "$(command -v sdasz80 2>/dev/null || echo /nonexistent/sdasz80)")}
[ -x "$SDCC/sdasz80" ] || { echo "sdasz80 が見つからない(PATH か SDCC_BIN= で指定)"; exit 1; }
# cpmsim は tizix のツリーにある arch/z80pack/cpmsim を使う(CPMSIM= で差し替え可)。
# 以前は ./cpmsim(tzcc/ 直下)を見ていたが、そこには無く、全テストが「出力なし」で
# FAIL していた(2026-09-25 時点で PASS=1 FAIL=44)。
CPMSIM=${CPMSIM:-../arch/z80pack/cpmsim}
[ -x "$CPMSIM" ] || { echo "cpmsim が無い: $CPMSIM(tizix で make するか CPMSIM= で指定)"; exit 1; }
mkdir -p "$BUILD"

make -s tzcc >/dev/null 2>&1 || { echo "tzcc build failed"; exit 1; }
cp crt0.s "$BUILD/crt0.s"
"$SDCC/sdasz80" -g -o "$BUILD/crt0.rel" "$BUILD/crt0.s" >/dev/null 2>&1 \
    || { echo "crt0.s assemble failed"; exit 1; }

if [ $# -gt 0 ]; then
    LIST=(); for n in "$@"; do LIST+=("tests/${n%.c}.c"); done
else
    LIST=(tests/ok_*.c)
fi

pass=0; fail=0; skip=0; unspec=0
FAILED=()

for c in "${LIST[@]}"; do
    name=$(basename "$c" .c)

    # 注記は \r を落として読む(CRLF のテスト file で期待値に \r が付き、見た目は
    # 同じなのに FAIL していた。ok_43..45 / ok_long1..2)。
    skipreason=$(sed -n 's#.*//[[:space:]]*RUN:[[:space:]]*skip[[:space:]]\{1,\}##p' "$c" | tr -d '\r' | head -1)
    if [ -n "$skipreason" ]; then
        printf '  SKIP  %-12s (%s)\n' "$name" "$skipreason"; skip=$((skip+1)); continue
    fi

    want_empty=$(grep -c '//[[:space:]]*EXPECT-EMPTY' "$c")
    mapfile -t exp < <(sed -n 's#.*//[[:space:]]*EXPECT:[[:space:]]\?##p' "$c" | tr -d '\r')
    if [ "${#exp[@]}" -eq 0 ] && [ "$want_empty" -eq 0 ]; then
        printf '  ----  %-12s (no EXPECT)\n' "$name"; unspec=$((unspec+1)); continue
    fi
    want=$(printf '%s\n' "${exp[@]}")
    [ "${#exp[@]}" -eq 0 ] && want=""

    # build (bare metal: crt0.s + _HEADER=0x0000 _CODE=0x0020)
    if ! ./tzcc -o "$BUILD/rt.s" "$c" 2>"$BUILD/rt.err"; then
        printf '  FAIL  %-12s (tzcc compile)\n' "$name"; fail=$((fail+1)); FAILED+=("$name"); continue
    fi
    if ! "$SDCC/sdasz80" -g -o "$BUILD/rt.rel" "$BUILD/rt.s" >"$BUILD/rt.err" 2>&1; then
        printf '  FAIL  %-12s (sdasz80)\n' "$name"; fail=$((fail+1)); FAILED+=("$name"); continue
    fi
    if ! "$SDCC/sdldz80" -i -b _HEADER=0x0000 -b _CODE=0x0020 \
            "$BUILD/rt.ihx" "$BUILD/crt0.rel" "$BUILD/rt.rel" >"$BUILD/rt.err" 2>&1; then
        printf '  FAIL  %-12s (sdldz80)\n' "$name"; fail=$((fail+1)); FAILED+=("$name"); continue
    fi

    got=$(timeout 15 "$CPMSIM" -z -x "$BUILD/rt.ihx" 2>/dev/null \
        | tr -d '\r' \
        | awk '/^Booting\.\.\./{b=1;next}
               b&&!p{p=1;next}
               p&&/System halted/{sub(/System halted.*/,""); if(length($0)>0) printf "%s\n",$0; exit}
               p{print}')

    if [ "$got" = "$want" ]; then
        printf '  PASS  %-12s\n' "$name"; pass=$((pass+1))
    else
        printf '  FAIL  %-12s\n' "$name"
        printf '        want: %s\n' "$(printf '%s' "$want" | sed -n '1,4p' | tr '\n' '|')"
        printf '        got : %s\n' "$(printf '%s' "$got"  | sed -n '1,4p' | tr '\n' '|')"
        fail=$((fail+1)); FAILED+=("$name")
    fi
done

echo "------------------------------------------------------------"
printf 'PASS=%d  FAIL=%d  SKIP=%d  (no-EXPECT=%d)\n' "$pass" "$fail" "$skip" "$unspec"
[ "$fail" -gt 0 ] && { echo "failed: ${FAILED[*]}"; exit 1; }
exit 0
