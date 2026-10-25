#!/usr/bin/env python3
"""tz/session.py IMAGE CMD... - boot IMAGE in QEMU, send each CMD at the shell prompt, print the transcript.
Used to compare the ia16-gcc kernel (disk/floppy.img) with the mixed tzcc kernel (disk/floppy_tz.img)."""
import os, pty, select, subprocess, sys, time

def main():
    img = sys.argv[1]
    cmds = sys.argv[2:]
    m, s = pty.openpty()
    p = subprocess.Popen(["qemu-system-i386", *(["-m", os.environ["TZ_QEMU_M"]] if os.environ.get("TZ_QEMU_M") else []), "-drive", f"file={img},format=raw,if=floppy",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-chardev", "stdio,id=c0,signal=off", "-serial", "chardev:c0",
        "-display", "none", "-no-reboot", "-boot", "a"], stdin=s, stdout=s, stderr=s)
    os.close(s)
    buf = b""
    def rd(pat, t):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([m], [], [], 0.1)
            if r:
                try: buf += os.read(m, 4096)
                except OSError: return False
            if pat.encode() in buf[rd.pos:]:
                rd.pos = len(buf); return True
        return False
    rd.pos = 0
    ok = rd("[/root]# ", 12)
    for c in cmds:
        if not ok: break
        os.write(m, (c + "\r").encode())
        ok = rd("[/root]# ", 12)
    p.kill()
    sys.stdout.write(buf.decode("latin1").replace("\r", ""))
    sys.exit(0 if ok else 1)
main()
