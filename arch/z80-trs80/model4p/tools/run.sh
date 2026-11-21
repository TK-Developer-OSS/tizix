#!/bin/sh
# run.sh DISK - run the disk image on xtrs (Model 4P).  The serial port (tizix's second console) is a pty; it is printed and
# bridged to this terminal, so the shell is usable from here.  XTRS = the xtrs binary, TZ_XDISPLAY = an X display to use
# (default: start Xvfb on :97; the screen shows in a window of that display only).
XTRS=${XTRS:-$HOME/tizix/work/xtrs/src/xtrs-4.9d/xtrs}
DISP=${TZ_XDISPLAY:-:97}
DISK=$(readlink -f "$1")
T=$(mktemp -d)
cp "$DISK" "$T/disk4p-0"
if [ -z "$TZ_XDISPLAY" ]; then Xvfb "$DISP" -screen 0 1024x768x24 >/dev/null 2>&1 & XV=$!; sleep 1; fi
python3 "$(dirname "$0")/run.py" "$XTRS" "$DISP" "$T"
[ -n "$XV" ] && kill $XV
rm -rf "$T"
