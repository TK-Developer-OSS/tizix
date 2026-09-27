#!/bin/sh
# docker/try/build.sh - tizix お試しイメージ(tizix-try)を作る
#
#   使い方(tizix のルートで): sh docker/try/build.sh
#     ビルド用イメージ tizix が無ければ先に docker/build.sh で作る。
#     作業ツリーをそのまま(git 管理情報・ビルド生成物・非公開の文書を除いて)固めて渡す。
#     公開するイメージは公開ツリー(~/tizix/dev)で作ること。
set -e
TIZIX=$(cd "$(dirname "$0")/../.." && pwd)
CTX=$TIZIX/docker/try/ctx
DOCKER=$(command -v docker || command -v podman)

"$DOCKER" image inspect tizix >/dev/null 2>&1 || sh "$TIZIX/docker/build.sh"

rm -rf "$CTX"; mkdir -p "$CTX"
tar cf "$CTX/src.tar" -C "$TIZIX" \
    --exclude=./.git --exclude=.gitignore --exclude=.gitattributes --exclude=.gitmodules \
    --exclude=.github --exclude=__pycache__ \
    --exclude=./build --exclude=./out --exclude=./docker/ctx --exclude=./docker/try/ctx \
    --exclude=./autom4te.cache --exclude=./config.log --exclude=./config.status \
    --exclude=./config.mk --exclude=./task.md --exclude=./DEVELOP.md \
    --exclude=./arch/z80pack/disks/drivea.dsk --exclude=./arch/z80pack/disks/driveb.dsk \
    --exclude=./arch/z80board/disks --exclude=./arch/z80board/sdcard.img \
    --exclude=./arch/z80pack/cpmsim --exclude=./arch/z80pack/receive \
    --exclude=./arch/z80board/z80boardsim --exclude=./arch/m68k-mega/m68ksim \
    --exclude=./arch/m68k-mega/rocket68 .
cp "$TIZIX/docker/try/Dockerfile" "$TIZIX/docker/try/entrypoint.sh" \
   "$TIZIX/docker/try/tizix" "$TIZIX/docker/try/README.txt" "$CTX/"

echo "=== $DOCKER build tizix-try ==="
"$DOCKER" build -t tizix-try "$CTX"
