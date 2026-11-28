#!/bin/sh
# update.sh - rebuild the x86-ia16 PC/XT floppy (360KB, FDD=pcxt) and copy it into this page as tizix-xt.img.
# The other files are third-party: pcx86.js and components.xsl/.css from PCjs 2.26 (MIT; components.xsl's two
# absolute paths made relative), GLaBIOS 0.4.2 PC/XT build GLABIOS_0.4.2_8X.ROM and font8x8_basic.h (bits mirrored,
# characters 0x80-0xFF blank) converted to PCjs's ROM JSON ({"width":8,"values":[...]}).
set -e
D=$(cd "$(dirname "$0")" && pwd)
cd "$D/../../arch/x86-ia16"
make -f Makefile.tz OBJ=obj/xt IMG=disk/floppy_xt.img FDD=pcxt all
cp disk/floppy_xt.img "$D/tizix-xt.img"
