#!/usr/bin/env python3
"""tz/ttytest.py IMAGE - the DRIVER's serial port (task #245). COM1 = the console (pty), COM2 = a unix socket.
Runs `ttyecho` and checks: READY arrives, a burst is echoed back whole, a second opener is refused, and bytes sent
while the shell is busy with the file system are not lost."""
import os, pty, select, socket, subprocess, sys, threading, time

img = sys.argv[1]
sock = "/tmp/claude-1000/-home-tk-tizix-test/2afc9c6e-c2ca-46c9-82b3-0e8b549cc627/scratchpad/com2.sock"
if os.path.exists(sock): os.unlink(sock)
m, s = pty.openpty()
p = subprocess.Popen(["qemu-system-i386", "-snapshot", "-drive", f"file={img},format=raw,if=floppy",
    "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    "-chardev", "stdio,id=c0,signal=off", "-serial", "chardev:c0",
    "-chardev", f"socket,id=c1,path={sock},server=on,wait=off", "-serial", "chardev:c1",
    "-display", "none", "-no-reboot", "-boot", "a"], stdin=s, stdout=s, stderr=s)
os.close(s)
buf = b""
def rd(pat, t):
    global buf
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
def cmd(c, t=15):
    os.write(m, (c + "\r").encode()); return rd("[/root]# ", t)
res = []
def check(name, ok):
    res.append(ok); print(("PASS " if ok else "FAIL ") + name)

check("boot", rd("[/root]# ", 15))
for _ in range(50):
    if os.path.exists(sock): break
    time.sleep(0.1)
c = socket.socket(socket.AF_UNIX); c.connect(sock); c.settimeout(0.2)
def rx(n, t):
    got = b""; end = time.time() + t
    while time.time() < end and len(got) < n:
        try: got += c.recv(4096)
        except socket.timeout: pass
    return got

# 1. READY and an echoed burst
os.write(m, b"ttyecho 8\r")
check("READY", rx(7, 5) == b"READY\r\n")
burst = bytes((i * 7 + 3) & 0xFF for i in range(200))
c.sendall(burst); time.sleep(0.5)
got = rx(200, 3)
check("burst echoed whole (%d/200)" % len(got), got == burst)
check("ttyecho finished", rd("ttyecho: 200 bytes, 0 lost", 12))
rd("[/root]# ", 5)

# 2. the second opener is refused while the first runs (the first is a background job)
os.write(m, b"ttyecho 6 &\r"); time.sleep(1.0)
rx(7, 2)
os.write(m, b"ttyecho 1\r")
check("second open refused", rd("ttyS1 is busy", 8))
rd("[/root]# ", 5); time.sleep(6)
rd("[/root]# ", 3)

# the owner ends without closing (kill): the port can be opened again
os.write(m, b"ttyecho 60 &\r"); time.sleep(1.0)
rx(7, 2)
rd("[/root]# ", 3); os.write(m, b"ps\r"); rd("BLK ST", 5); rd("[/root]# ", 5)
import re
pids = [int(x) for x in re.findall(r"(?m)^(\d+) (?:rdy|run)", buf.decode("latin1")[-300:])]
print("ps slots:", pids)
os.write(m, b"kill %d\r" % max(pids)); rd("killed", 5); rd("[/root]# ", 5)
os.write(m, b"ttyecho 1\r"); t_k = time.time()
ok_ = rd("ttyecho: 0 bytes", 30)
check("reopen after kill (%.1fs for a 1s run)" % (time.time() - t_k), ok_)
if not ok_: print(repr(buf.decode("latin1")[-250:]))
rd("[/root]# ", 5)

# paced traffic with a reader running at the same time (a reader that stops reading stalls the guest's own transmit)
def traffic(secs, gap, wait):
    d = bytes(range(32, 76)); sent = [0]; got = [b""]
    stop = time.time() + secs + wait
    def sender():
        t0 = time.time()
        while time.time() - t0 < secs:
            c.sendall(d); sent[0] += len(d); time.sleep(gap)
    th = threading.Thread(target=sender); th.start()
    while time.time() < stop:
        try: got[0] += c.recv(4096)
        except socket.timeout: pass
    th.join()
    return sent[0], len(got[0])

# 3. nothing else running, then with the file system busy
for load in (0, 1):
    os.write(m, b"ttyecho 9 &\r"); time.sleep(1.0)
    rx(7, 2)
    if load:
        for k in range(6):
            os.write(m, b"cp /bin/vi.bin /bin/v%d.tmp\r" % k)
    sent, back = traffic(3, float(os.environ.get("GAP", "0.05")), 3)
    check("echo %s: %d/%d" % ("during cp" if load else "idle", back, sent), back == sent)
    rd("[/root]# ", 12); time.sleep(4); rd("ttyecho:", 5)
    if load:
        for k in range(6): cmd("rm /bin/v%d.tmp" % k)
p.kill()
print("buf tail:", buf.decode("latin1").replace("\r", "")[-200:])
sys.exit(0 if all(res) else 1)
