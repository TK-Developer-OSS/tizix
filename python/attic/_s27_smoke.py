#!/usr/bin/env python3
"""#27 S1 smoke: fresh boot, single pipe cmd, then a few basics."""
import os, sys, time, select, pty, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

tzpaths.wait_disk_ready(tzpaths.DRIVEB)
master, slave = pty.openpty()
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)

def ru(pat, to=6.0):
    buf = b""; t0 = time.time()
    while time.time() - t0 < to:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try: c = os.read(master, 4096)
            except OSError: break
            if not c: break
            buf += c
            if pat.encode() in buf: break
    return buf.decode("latin-1", "replace")

def cmd(s, pat="]# ", to=8.0):
    os.write(master, (s + "\n").encode())
    return ru(pat, to)

print(ru("[/root]# ", 6.0))
for c in sys.argv[1:]:
    print(f"$ {c}")
    print(cmd(c))
os.write(master, b"exit\n"); time.sleep(0.3); proc.terminate()
