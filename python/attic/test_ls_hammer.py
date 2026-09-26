#!/usr/bin/env python3
# test_ls_hammer.py - 外部 ls を連続実行して flaky crash を再現/検証する診断用
import os, sys, pty, time, select, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

tzpaths.wait_disk_ready(tzpaths.DRIVEB)
master, slave = pty.openpty()
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout=4.0):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 4096)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                break
    return buf.decode("latin-1", "replace")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
read_until("\x00" * 99, 0.5)
N = 30
bad = 0
for i in range(N):
    os.write(master, b"ls /bin\n")            # コマンドは /bin。外部 ls を連打して crash 監視
    out = read_until("]# ", 4.0)
    if "trap" in out or "HALT" in out or "Op-code" in out:
        print(f"[{i}] CRASH: {out!r}")
        bad += 1
        break
    if "hello.bin" not in out:
        print(f"[{i}] bad output: {out!r}")
        bad += 1
print(f"ran {i+1}/{N}, bad={bad}")
os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
sys.exit(1 if bad else 0)
