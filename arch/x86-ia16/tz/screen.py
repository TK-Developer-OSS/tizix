#!/usr/bin/env python3
"""tz/screen.py IMAGE [KEY ...] - boot IMAGE, send keys through the QEMU monitor (exercises the 8042 poll path),
then print the 80x25 VGA text screen read from B800:0000.  Compare the ia16-gcc and tzcc kernels with it."""
import os, re, socket, subprocess, sys, tempfile, time

def main():
    img = sys.argv[1]
    keys = sys.argv[2:]
    sock = tempfile.mktemp(suffix=".sock")
    p = subprocess.Popen(["qemu-system-i386", *(["-m", os.environ["TZ_QEMU_M"]] if os.environ.get("TZ_QEMU_M") else []), "-drive", f"file={img},format=raw,if=floppy",
        "-display", "none", "-no-reboot", "-boot", "a", "-serial", os.environ.get("TZ_SERIAL", "null"),
        "-monitor", f"unix:{sock},server,nowait"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    s = socket.socket(socket.AF_UNIX); s.connect(sock); s.settimeout(0.5)
    def cmd(c, wait=0.3):
        s.sendall((c + "\n").encode()); time.sleep(wait)
        out = b""
        try:
            while True:
                d = s.recv(65536)
                if not d: break
                out += d
        except socket.timeout: pass
        return out.decode("latin1")
    time.sleep(3.0)
    for k in keys:
        cmd("sendkey " + k, 0.15)
    time.sleep(1.5)
    raw = cmd("xp /4000bx 0xb8000", 1.0)
    p.kill()
    bs = []
    for ln in raw.replace("\r", "").split("\n"):
        m = re.match(r"^[0-9a-f]{8,16}:((?: 0x[0-9a-f]{2})+)\s*$", ln)
        if m: bs += [int(x, 16) for x in m.group(1).split()]
    chars = bs[0::2][:2000]
    for r in range(25):
        line = "".join(chr(c) if 32 <= c < 127 else " " for c in chars[r*80:(r+1)*80]).rstrip()
        if line: print(line)
main()
