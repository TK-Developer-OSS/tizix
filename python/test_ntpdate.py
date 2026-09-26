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

def run_cmd(cmd, timeout=6.0):
    print(f"\n--- Running: {cmd} ---")
    os.write(master, (cmd + "\n").encode())
    res = read_until("]# ", timeout)
    print(res.decode(errors='ignore'))

run_cmd("date")
run_cmd("ntpdate", timeout=8.0)
run_cmd("date")

proc.terminate()
