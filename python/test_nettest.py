#!/usr/bin/env python3
"""net.bin常駐デーモン+netcli の実証用使い捨てテスト。"""
import os, pty, select, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

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

pump(b"System Driver .. LOADED", 30.0)
pump(b"# ", 5.0)

print("\n--- net & (launching) ---")
os.write(master, b"net &\r\n")
time.sleep(1) 
os.write(master, b"ps\r\n")
time.sleep(1)

print("\n--- nettest ---")
os.write(master, b"nettest\r\n")
out, ok = pump(b"nettest: done", 15.0)

# 受信したバッファ全体を表示
print(f"\n[DEBUG] buf_all: {buf_all}")

if b"PONG-FROM-HOST" in buf_all:
    print("\n*** PASS: host からの応答をゲスト側で受信できた ***")
else:
    print("\n*** FAIL: PONG-FROM-HOST が確認できなかった ***")

time.sleep(0.5)
os.write(master, b"\x03")
time.sleep(0.5)
try:
    proc.terminate()
except Exception:
    pass
