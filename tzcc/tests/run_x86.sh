#!/bin/bash
# run_x86.sh - tests/ok_*.c を tzcc の x86-64 バックエンドでコンパイルし、
#              gcc でリンクしてネイティブ実行、// EXPECT: と実出力を照合する。
set -u
cd "$(dirname "$0")/.."
BUILD=arch/z80pack
mkdir -p "$BUILD"
make -s tzcc >/dev/null 2>&1 || { echo "tzcc build failed"; exit 1; }

if [ $# -gt 0 ]; then LIST=(); for n in "$@"; do LIST+=("tests/${n%.c}.c"); done
else LIST=(tests/ok_*.c); fi

pass=0; fail=0; skip=0; unspec=0
FAILED=()
for c in "${LIST[@]}"; do
    name=$(basename "$c" .c)
    sr=$(sed -n 's#.*//[[:space:]]*RUN:[[:space:]]*skip[[:space:]]\{1,\}##p' "$c" | head -1)
    sx=$(sed -n 's#.*//[[:space:]]*RUN:[[:space:]]*skip-x86[[:space:]]\{1,\}##p' "$c" | head -1)
    if [ -n "$sr" ] || [ -n "$sx" ]; then
        printf '  SKIP  %-12s (%s)\n' "$name" "${sr:-$sx}"; skip=$((skip+1)); continue; fi
    we=$(grep -c '//[[:space:]]*EXPECT-EMPTY' "$c")
    mapfile -t exp < <(sed -n 's#.*//[[:space:]]*EXPECT:[[:space:]]\?##p' "$c")
    if [ "${#exp[@]}" -eq 0 ] && [ "$we" -eq 0 ]; then
        printf '  ----  %-12s (no EXPECT)\n' "$name"; unspec=$((unspec+1)); continue; fi
    want=$(printf '%s\n' "${exp[@]}"); [ "${#exp[@]}" -eq 0 ] && want=""

    if ! ./tzcc --march=x86 -o "$BUILD/xt.s" "$c" 2>"$BUILD/xt.err"; then
        printf '  FAIL  %-12s (tzcc)\n' "$name"; fail=$((fail+1)); FAILED+=("$name"); continue; fi
    if ! gcc -no-pie -w -o "$BUILD/xt" "$BUILD/xt.s" 2>"$BUILD/xt.err"; then
        printf '  FAIL  %-12s (gcc link)\n' "$name"; fail=$((fail+1)); FAILED+=("$name"); continue; fi
    got=$(timeout 10 "$BUILD/xt" 2>/dev/null)
    if [ "$got" = "$want" ]; then printf '  PASS  %-12s\n' "$name"; pass=$((pass+1))
    else
        printf '  FAIL  %-12s\n' "$name"
        printf '        want: %s\n' "$(printf '%s' "$want" | tr '\n' '|')"
        printf '        got : %s\n' "$(printf '%s' "$got"  | tr '\n' '|')"
        fail=$((fail+1)); FAILED+=("$name")
    fi
done
echo "------------------------------------------------------------"
printf 'x86  PASS=%d  FAIL=%d  SKIP=%d  (no-EXPECT=%d)\n' "$pass" "$fail" "$skip" "$unspec"
[ "$fail" -gt 0 ] && { echo "failed: ${FAILED[*]}"; exit 1; }
exit 0
