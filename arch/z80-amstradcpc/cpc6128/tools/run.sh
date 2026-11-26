#!/bin/sh
# run.sh DISK - run the disk image on MAME as a CPC 6128 in its own window (needs an X display: DISPLAY).
# |CPM is typed for you at the BASIC prompt and tizix boots from the disk; do not press keys until the [/root]# prompt.
# The host keys stand for the CPC keys at the same place (MAME's default keyboard mode), so the symbols are the ones
# printed on a CPC: Shift + 6 = &, Shift + 2 = ", the key right of P = @ (Shift: |).  A Japanese keyboard has almost
# the same layout.  Ctrl + C = Break.  Scroll Lock switches between the CPC keyboard and MAME's own keys (Tab = menu,
# Esc = quit).
# MAME = the binary (default mame); it needs cpc6128.rom and cpcados.rom on its rompath (see
# python/test_z80_amstradcpc.py).  Caprice32 also runs the disk (cap32 -a "|cpm<Enter>" DISK), but it releases the
# wrong key when Shift goes up before a shifted key, which leaves a CPC key held down.
# The disk is copied first, so the repository's image is never changed.
MAME=${MAME:-mame}
command -v "$MAME" >/dev/null || { echo "run.sh: no MAME (set MAME)"; exit 1; }
[ -n "$DISPLAY" ] || { echo "run.sh: no DISPLAY (MAME needs a window)"; exit 1; }
T=$(mktemp -d)
cp "$1" "$T/cpc.dsk"
"$MAME" cpc6128 -window -nomaximize -skip_gameinfo -flop1 "$T/cpc.dsk" -autoboot_command '|cpm\n' -autoboot_delay 3
rm -rf "$T"
