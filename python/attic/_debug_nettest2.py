#!/usr/bin/env python3
"""Extended debug harness: longer pumps, a real host-side server that
accepts the connection and sends PONG-FROM-HOST\\n back, then watches
for crash indicators (CPU stopped / trap) after nettest runs."""
import os, pty, select, subprocess, sys, time, socket, threading

sys.path.insert(0, os.path.expanduser("~/z80pack/tizix/python"))
import tzpaths

def server_thread():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 8080))
    srv.listen(1)
    srv.settimeout(20)
    try:
        conn, addr = srv.accept()
    except socket.timeout:
        print("[host-srv] accept timeout")
        return
    conn.settimeout(5)
    try:
        data = conn.recv(4096)
        print(f"[host-srv] received: {data!r}")
    except Exception as e:
        print(f"[host-srv] recv error: {e}")
    try:
        conn.sendall(b"PONG-FROM-HOST\n")
        print("[host-srv] sent PONG-FROM-HOST")
    except Exception as e:
        print(f"[host-srv] send error: {e}")
    time.sleep(1)
    conn.close()
    srv.close()

th = threading.Thread(target=server_thread, daemon=True)
th.start()

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

print("\n--- net & ---")
os.write(master, b"net &\r\n")
time.sleep(1)

print("\n--- nettest (extended 25s capture) ---")
os.write(master, b"nettest\r\n")

out, ok = pump(b"nettest: done", 25.0)
print(f"\n[RESULT] matched nettest:done = {ok}")

alive = proc.poll() is None
print(f"[RESULT] cpmsim process alive = {alive}")

time.sleep(1)
os.write(master, b"\r\nps\r\n")
pump(b"# ", 5.0)

if b"PONG-FROM-HOST" in buf_all:
    print("\n*** PASS: guest received PONG-FROM-HOST ***")
else:
    print("\n*** FAIL: PONG-FROM-HOST not seen in guest output ***")

os.write(master, b"\x03")
time.sleep(0.5)
try:
    proc.terminate()
except Exception:
    pass
