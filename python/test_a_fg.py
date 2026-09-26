import pty
import os
import time
import subprocess
import select

master, slave = pty.openpty()
proc = subprocess.Popen(
    ['./cpmsim', '-d', 'disks'],
    stdin=slave,
    stdout=slave,
    stderr=slave,
    cwd=os.path.expanduser('~/z80pack/tizix/arch/z80pack'),
    close_fds=True
)
os.close(slave)

time.sleep(1.0)
print(os.read(master, 1024).decode(errors='ignore'))

print("Sending: a")
os.write(master, b"a\n")

for _ in range(5):
    time.sleep(1.0)
    try:
        r, _, _ = select.select([master], [], [], 0.5)
        if master in r:
            print(os.read(master, 2048).decode(errors='ignore'), end="")
    except Exception as e:
        print("error:", e)

proc.terminate()
