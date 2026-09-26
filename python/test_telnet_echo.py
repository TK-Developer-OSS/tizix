#!/usr/bin/env python3
# test_telnet_echo.py - telnet(net.bin 常駐 + netcli)をホストのエコーサーバで通す(#50)
#
#   ホスト側に 127.0.0.1:PORT のサーバを立てる: 接続時に "WELCOME" を送り、
#   以後 1 行ごとに "ECHO:<行>" を返す。ゲストで
#     net &  →  telnet 127.0.0.1 PORT  →  hello + Enter  →  Ctrl+C
#   を流し、受信・送信・切断・プロンプト復帰を確認する。z80pack(cpmsim)用。
import os
import sys
import pty
import time
import select
import socket
import threading
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

PORT = 18023
got_lines = []


def server():
    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", PORT))
    ls.listen(1)
    ls.settimeout(60)
    try:
        c, _ = ls.accept()
    except socket.timeout:
        return
    c.sendall(b"WELCOME\r\n")
    buf = b""
    c.settimeout(30)
    try:
        while True:
            d = c.recv(256)
            if not d:
                break
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.rstrip(b"\r")
                got_lines.append(line)
                c.sendall(b"ECHO:" + line + b"\r\n")
    except socket.timeout:
        pass
    c.close()
    ls.close()


threading.Thread(target=server, daemon=True).start()
time.sleep(0.3)

tzpaths.wait_disk_ready(tzpaths.DRIVEB)
master, slave = pty.openpty()
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout):
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


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + detail))
    if not cond:
        fails += 1


read_until("]# ", 30.0)
os.write(master, b"net &\r")
read_until("]# ", 5.0)
time.sleep(1.0)

os.write(master, ("telnet 127.0.0.1 %d\r" % PORT).encode())
out = read_until("WELCOME", 15.0)
print(out.replace("\r", ""))
check("connected", "telnet: connected" in out, repr(out[-120:]))
check("connecting 行が 1 行で出る",
      ("telnet: connecting to 127.0.0.1:%d ...\r\n" % PORT) in out, repr(out[:160]))
check("サーバからの受信(WELCOME)", "WELCOME" in out, repr(out[-120:]))

for ch in "hello":
    os.write(master, ch.encode())
    time.sleep(0.05)
os.write(master, b"\r")
out = read_until("ECHO:hello", 10.0)
print(out.replace("\r", ""))
check("送信 → 応答(ECHO:hello)", "ECHO:hello" in out, repr(out[-120:]))
check("サーバが受けた行 = hello", got_lines[:1] == [b"hello"], repr(got_lines))

os.write(master, b"\x03")
out = read_until("]# ", 10.0)
print(out.replace("\r", ""))
check("Ctrl+C で切断しプロンプトへ", "telnet: closed" in out and out.endswith("]# "), repr(out[-120:]))

proc.kill()
print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
