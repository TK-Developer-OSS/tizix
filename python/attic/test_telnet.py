import pty
import os
import time
import subprocess
import select

master, slave = pty.openpty()
proc = subprocess.Popen(
    ['./cpmsim'],
    stdin=slave,
    stdout=slave,
    stderr=slave,
    cwd=os.path.expanduser('~/z80pack/tizix/arch/z80pack'),
    close_fds=True
)
os.close(slave)

def read_until(pat, timeout=8.0):
    buf = b""
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 1024)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                return buf
    return buf

print("Waiting for boot...")
out = read_until("]# ", 8.0)
print(out.decode(errors='ignore'))

print("\n--- Running: telnet 127.0.0.1 8080 ---")
os.write(master, b"telnet 127.0.0.1 8080\n")
time.sleep(0.5)
# at_modem.py へ AT コマンドを送ってみる(生テキスト、CIPSTART等はしない)
os.write(master, b"AT\r\n")
res = read_until("OK", 3.0)
print(res.decode(errors='ignore'))
# Ctrl+C で終了
os.write(master, b"\x03")
res = read_until("]# ", 3.0)
print(res.decode(errors='ignore'))

proc.terminate()
