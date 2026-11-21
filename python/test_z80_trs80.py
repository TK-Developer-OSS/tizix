#!/usr/bin/env python3
"""test_z80_trs80.py - end-to-end test of arch/z80-trs80/model4p on xtrs (Model 4P).
  make ARCH=z80-trs80 builds disks/disk4p-0; xtrs boots it on its own Xvfb display, with the serial port on a pty
  (the kernel's second console); the shell is driven through the pty.
  XTRS = the xtrs binary (default ~/tizix/work/xtrs/src/xtrs-4.9d/xtrs).  Usage: python3 python/test_z80_trs80.py"""
import os, pty, select, shutil, subprocess, sys, tempfile, time, tty

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XTRS = os.environ.get("XTRS", os.path.expanduser("~/tizix/work/xtrs/src/xtrs-4.9d/xtrs"))
DISP = os.environ.get("TZ_XDISPLAY", ":97")
subprocess.check_call(["make", "-s", "ARCH=z80-trs80", "DEVICE=model4p"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
tmp = tempfile.mkdtemp()
shutil.copy(os.path.join(ROOT, "arch/z80-trs80/model4p/disks/disk4p-0"), tmp + "/disk4p-0")
xv = None
if "TZ_XDISPLAY" not in os.environ:
    xv = subprocess.Popen(["Xvfb", DISP, "-screen", "0", "1024x768x24"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
m, s = pty.openpty()
tty.setraw(m); tty.setraw(s)
p = subprocess.Popen([XTRS, "-model", "4p", "-diskdir", tmp, "-serial", os.ttyname(s)],
                     env=dict(os.environ, DISPLAY=DISP), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
buf = b""

def rd(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([m], [], [], 0.2)
        if r:
            buf += os.read(m, 4096)

def run(line, wait=4):
    global buf
    buf = b""
    for ch in line + "\r":
        os.write(m, ch.encode()); time.sleep(0.03)
    end = time.time() + wait
    while time.time() < end:
        rd(0.3)
        if buf.rstrip().endswith(b"]#"): break
    return buf.decode(errors="replace")

rd(25)
boot = buf.decode(errors="replace")
print("boot output tail:", boot[-120:].replace("\r", "\\r").replace("\n", "\\n"))
tests = [
    ("echo abc | cat",        "abc"),
    ("ls /bin | wc",          None),
    ("echo hi > /root/t.txt", None),
    ("cat /root/t.txt",       "hi"),
    ("cat /etc/rc",           "rsyslog"),
    ("uptime",                "up "),
]
fail = 0
for line, want in tests:
    out = run(line)
    ok = (want is None and "EXC" not in out) or (want is not None and want in out and "EXC" not in out)
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print(out)
p.kill(); 
if xv: xv.kill()
shutil.rmtree(tmp, ignore_errors=True)
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
