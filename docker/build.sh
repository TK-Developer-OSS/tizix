#!/bin/sh
# docker/build.sh - tizix ビルド・回帰用イメージを作る(#74)
#
#   使い方(tizix のルートで): sh docker/build.sh [z80pack-tizix の場所]
#     既定は ../z80pack-tizix。docker が無ければ podman を使う。
#
#   ビルドコンテキストは docker/ctx/ に組む。フォークは **コミット済みの HEAD** を
#   git archive で固める(作業ツリーの未コミット変更は入らない)。rocket68 は
#   arch/m68k-mega/rocket68 の手元コピーをそのまま入れる(リポジトリには含まれない)。
set -e
TIZIX=$(cd "$(dirname "$0")/.." && pwd)
FORK=${1:-$TIZIX/../z80pack-tizix}
CTX=$TIZIX/docker/ctx
DOCKER=$(command -v docker || command -v podman)

[ -d "$FORK/.git" ] || { echo "z80pack-tizix が見つからない: $FORK"; exit 1; }
[ -d "$TIZIX/arch/m68k-mega/rocket68/src" ] || { echo "arch/m68k-mega/rocket68 が無い(m68ksim の CPU コア)"; exit 1; }

rm -rf "$CTX"; mkdir -p "$CTX"
REV=$(git -C "$FORK" rev-parse --short HEAD)
git -C "$FORK" archive --format=tar --prefix=z80pack-tizix/ HEAD > "$CTX/z80pack-tizix.tar"
tar cf "$CTX/rocket68.tar" -C "$TIZIX/arch/m68k-mega/rocket68" \
    --exclude=./obj --exclude=./lib --exclude=./.zig-cache --exclude=./zig-out .
cp "$TIZIX/docker/Dockerfile" "$TIZIX/docker/entrypoint.sh" "$CTX/"

echo "=== $DOCKER build (z80pack-tizix $REV) ==="
"$DOCKER" build --build-arg FORK_REV="$REV" -t tizix "$CTX"
