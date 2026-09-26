#!/usr/bin/env python3
"""Robust ad-hoc cpmsim driver for tizix (NOT a repo artifact).
Usage: python3 cpm_run.py "cmd1" "cmd2" ...
Boots a fresh ./cpmsim, waits for the shell, sends each command, prints all output.
"""
import os, pty, select, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

cmds = sys.argv[1:] or ["hello"]

if not tzpaths.wait_disk_ready(tzpaths.DRIVEB):
    sys.stderr.write("cpm_run: driveb.dsk not settled after timeout; launching anyway\n")

master, slave = pty.openpty()
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)

buf_all = b""

def pump(pattern, timeout):
    global buf_all
    local = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.2)
        if r:
            try:
                c = os.read(master, 4096)
            except OSError:
                return local, False
            if not c:
                return local, False
            local += c
            buf_all += c
            sys.stdout.write(c.decode("latin-1", "replace"))
            sys.stdout.flush()
            if pattern and pattern in local:
                return local, True
    return local, False

# generous boot wait
out, ok = pump(b"System Driver .. LOADED", 30.0)
pump(b"# ", 5.0)
if b"DETECTED" not in buf_all:
    sys.stdout.write("\n*** boot: DETECTED not seen (continuing anyway) ***\n")

for cmd in cmds:
    time.sleep(0.3)
    os.write(master, (cmd + "\r\n").encode("latin-1"))
    pump(b"# ", 12.0)

time.sleep(0.3)
os.write(master, b"exit\r\n")
time.sleep(0.5)
try:
    proc.terminate()
except Exception:
    pass
sys.stdout.write("\n--- done ---\n")
