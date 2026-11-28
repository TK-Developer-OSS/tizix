#!/bin/sh
# run.sh DISK - run the Compact Flash image on MAME as an RC2014 Zed Pro (rc2014zedp: RomWBW 3.0.1, 512K RAM / 512K
# flash, SIO/2, a Compact Flash module), with this terminal as its serial terminal, as on a real RC2014: SIO/2 channel A
# is on a null modem whose other end is a TCP socket, and socat joins this terminal to it (raw, 115200 8N1 on the
# RC2014 side).  MAME runs without a window (and without an X display).
#   At RomWBW's "Boot Selection?" type 2 (the CF card, IDE0), then Enter at "Slice".  Ctrl+] leaves (and stops MAME).
#   The Dual Clock module's CLOCK2 is set to 0.3072MHz (a MAME config file): SIO/2 channel B is tizix's timer.
# MAME = the binary (default mame); it needs RomWBW's RCZ80_std.rom (3.0.1) as rc2014zedp/rcz80_std_3_0_1.rom on its
# rompath, e.g. ~/mame/roms/rc2014zedp/ (the RomWBW release package has it as Binary/RCZ80_std.rom).  PORT = the TCP
# port (default 20140).  The disk is copied first, so the repository's image is never changed.
MAME=${MAME:-mame}
PORT=${PORT:-20140}
command -v "$MAME" >/dev/null || { echo "run.sh: no MAME (set MAME)"; exit 1; }
command -v socat >/dev/null || { echo "run.sh: no socat"; exit 1; }
T=$(mktemp -d)
cp "$1" "$T/cf.hd"
mkdir "$T/cfg"
cat > "$T/cfg/rc2014zedp.cfg" <<'EOF'
<?xml version="1.0"?>
<mameconfig version="10">
    <system name="rc2014zedp">
        <input>
            <port tag=":bus:2:dual_clk:CLOCK2" type="CONFIG" mask="15" defvalue="0" value="6" />
        </input>
    </system>
</mameconfig>
EOF
echo "RC2014 on MAME: at \"Boot Selection?\" type 2, then Enter.  Ctrl+] leaves."
# MAME connects to the socket socat listens on: start it a moment later.  No X display for it: with one, MAME (SDL)
# opens an empty full-screen window even with -video none.  It takes about one CPU core: nice keeps the desktop smooth.
( sleep 1; cd "$T" && exec env -u DISPLAY -u WAYLAND_DISPLAY nice -n 19 "$MAME" rc2014zedp -bus:3 rom_ram,bios=3.0.1 -bus:5 cf -hard "$T/cf.hd" \
    -bus:4:sio:rs232a null_modem -bitb "socket.127.0.0.1:$PORT" -cfg_directory "$T/cfg" \
    -video none -sound none -skip_gameinfo >"$T/mame.log" 2>&1 ) &
M=$!
socat -,raw,echo=0,escape=0x1d "TCP-LISTEN:$PORT,bind=127.0.0.1,reuseaddr"
# socat is done (Ctrl+]): stop MAME (the subshell exec'd it, so $M is MAME itself)
kill "$M" 2>/dev/null
wait "$M" 2>/dev/null
stty sane 2>/dev/null
echo
rm -rf "$T"
