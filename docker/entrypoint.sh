#!/bin/sh
# tizix-entry: /tizix にマウントされたツリーへ、イメージ内のシミュレータと
# rocket68 を(無ければ)置いてから、渡されたコマンドを実行する。
set -e
cd /tizix
if [ ! -f Makefile ] || [ ! -d arch ]; then
    echo "tizix-entry: /tizix に tizix のツリーがマウントされていない"
    echo "  例: docker run --rm -v \"\$PWD\":/tizix tizix"
    exit 1
fi
F=/opt/z80pack-tizix
[ -x arch/z80pack/cpmsim ]       || cp "$F/cpmsim/cpmsim"           arch/z80pack/cpmsim
[ -x arch/z80pack/receive ]      || cp "$F/cpmsim/srctools/receive" arch/z80pack/receive
[ -x arch/z80board/z80boardsim ] || cp "$F/z80boardsim/z80boardsim" arch/z80board/z80boardsim
# 所有者は引き継がない(cp -a だと rootless podman でホストから消せない uid のファイルが残った)
[ -d arch/m68k-mega/rocket68/src ] || { mkdir -p arch/m68k-mega/rocket68 && cp -R /opt/rocket68/. arch/m68k-mega/rocket68/; }
exec "$@"
