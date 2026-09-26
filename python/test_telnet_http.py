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

def read_until(pat, timeout=10.0):
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

print("\n--- Running: telnet www.kernel.org 80 ---")
os.write(master, b"telnet www.kernel.org 80\n")
time.sleep(1.0)
# GET / HTTP/1.0<Enter>Host: www.kernel.org<Enter><Enter>
os.write(master, b"GET / HTTP/1.0\n")
time.sleep(0.3)
os.write(master, b"Host: www.kernel.org\n")
time.sleep(0.3)
os.write(master, b"\n")
res = read_until("HTTP/", 6.0)
print(res.decode(errors='ignore'))
time.sleep(1.0)
os.write(master, b"\x03")
res = read_until("]# ", 5.0)
print(res.decode(errors='ignore'))

proc.terminate()
