#!/usr/bin/env python3
"""tz/tickrate.py IMG KWORK_ADDR - count timer ticks (kwork+359) per 4 wall-clock seconds at the idle prompt."""
import re, socket, subprocess, sys, tempfile, time
img, addr = sys.argv[1], int(sys.argv[2], 16)
sock = tempfile.mktemp(suffix=".sock")
p = subprocess.Popen(["qemu-system-i386", "-drive", f"file={img},format=raw,if=floppy", "-display", "none", "-no-reboot",
    "-boot", "a", "-serial", "null", "-monitor", f"unix:{sock},server,nowait"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1); s = socket.socket(socket.AF_UNIX); s.connect(sock); s.settimeout(0.5)
def rd():
    s.sendall(f"xp /2bx {0x10000 + addr + 359}\n".encode()); time.sleep(0.3)
    raw = b""
    while True:
        try: d = s.recv(65536)
        except Exception: break
        if not d: break
        raw += d
    b = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", raw.decode("latin1").split("\r\n", 1)[-1])]
    return b[0] | b[1] << 8
time.sleep(3); a = rd(); t0 = time.time(); time.sleep(4); b = rd(); dt = time.time() - t0
p.kill()
print(f"{img}: {((b - a) & 0xffff) / dt:.1f} ticks/s")
