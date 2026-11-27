#!/bin/sh
# run.sh DISK - run the disk image on MAME as a ZX Spectrum +3 (specpls3) in its own window (needs an X display:
# DISPLAY).  tools/mamezx.lua boots the disk (MAME's +3 ROM Loader does not) and types for you: every key you press
# becomes the character it gives on your keyboard (TZ_KBD = jp, the default, or us), typed as the Spectrum keys that
# give it in tizix, so '&', '|', ':', '-' and the rest work as printed on the host keyboard.  Esc = Esc (BREAK), the
# arrow keys, Backspace and Ctrl + letter work too (Ctrl + C = Break); Alt + a key = Symbol Shift + that key (the
# symbol on the Spectrum keycap), which also reaches what MAME cannot see on a Japanese keyboard (the yen and ro keys:
# | = Alt + S, \ = Alt + D, _ = Alt + 0).  The disk is a real one: loading takes the time a +3 would take.
# Scroll Lock gives MAME's own keys back (Tab = menu, Esc = quit).
# MAME = the binary (default mame); it needs the +3 ROMs 40092.ic7 and 40093.ic8 on its rompath, e.g.
# ~/mame/roms/specpls3/ (ZEsarUX ships them as one file, p2a40.rom: its first and second 32KB).
# The disk is copied first, so the repository's image is never changed.
MAME=${MAME:-mame}
command -v "$MAME" >/dev/null || { echo "run.sh: no MAME (set MAME)"; exit 1; }
[ -n "$DISPLAY" ] || { echo "run.sh: no DISPLAY (MAME needs a window)"; exit 1; }
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
cp "$1" "$T/disk3.dsk"
TZ_ZXBOOT="$T/disk3.dsk" "$MAME" specpls3 -window -nomaximize -skip_gameinfo -flop "$T/disk3.dsk" \
    -autoboot_script "$HERE/mamezx.lua"
rm -rf "$T"
