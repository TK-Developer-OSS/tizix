#!/bin/sh
# run.sh DISK - run the disk image on ZEsarUX as a ZX Spectrum +3 in its own window (needs an X display: DISPLAY).
# The disk boots by itself (the DSK given on the command line is auto-loaded through the +3 menu at top speed: do not
# press keys until the [/root]# prompt); type on the host keyboard:
#   Symbol Shift = host Ctrl or Alt (e.g. '&' = Alt + 6, ':' = Alt + Z), Caps Shift = host Shift.
#   Caps Shift + 5 6 7 8 = cursor keys, Caps Shift + 0 = backspace, Symbol Shift + a key = the symbol on its keycap.
# ZESARUX = the binary (default: zesarux on PATH, else /mnt/sda1/zx/ZEsarUX-13.0/zesarux), ZX_LIBS = a directory with
# libSDL-1.2.so.0 when the system has none.  The disk is copied first, so the repository's image is never changed.
ZESARUX=${ZESARUX:-$(command -v zesarux || echo /mnt/sda1/zx/ZEsarUX-13.0/zesarux)}
ZX_LIBS=${ZX_LIBS:-/mnt/sda1/zx/lib/usr/lib/x86_64-linux-gnu}
[ -d "$ZX_LIBS" ] && export LD_LIBRARY_PATH="$ZX_LIBS:$LD_LIBRARY_PATH"
[ -n "$DISPLAY" ] || { echo "run.sh: no DISPLAY (ZEsarUX needs a window)"; exit 1; }
T=$(mktemp -d)
cp "$1" "$T/disk3.dsk"
"$ZESARUX" --noconfigfile --ao null --nowelcomemessage --fastautoload --machine P341 --enable-dsk --dsk-file "$T/disk3.dsk" --zoom 2 "$T/disk3.dsk"
rm -rf "$T"
