import os
import pty
import subprocess
import select
import time
import sys

master, slave = pty.openpty()
proc = subprocess.Popen(
    ['./cpmsim'],
    cwd=os.path.expanduser('~/z80pack/tizix/arch/z80pack'),
    stdin=slave,
    stdout=slave,
    stderr=slave,
    close_fds=True
)
os.close(slave)

t0 = time.time()
output = b''
step = 0

while time.time() - t0 < 20:      # ディスクは永続 → 後始末(rm COPY.BIN)まで回す
    r, _, _ = select.select([master], [], [], 0.05)
    if r:
        try:
            chunk = os.read(master, 1024)
            if not chunk:
                break
            output += chunk
            text = chunk.decode('latin-1', 'replace')
            sys.stdout.write(text)
            sys.stdout.flush()
        except OSError:
            break

        if step == 0 and b'# ' in output:
            time.sleep(0.3)
            os.write(master, b'cp /bin/hello.bin copy.bin\r\n')  # commands live in /bin now
            step = 1
            output = b''
        elif step == 1 and b'# ' in output:
            time.sleep(0.3)
            os.write(master, b'ls\r\n')       # copy.bin が /root にできたか
            step = 2
            output = b''
        elif step == 2 and b'# ' in output:
            time.sleep(0.3)
            os.write(master, b'rm copy.bin\r\n')   # 後始末(ディスクは永続なので)
            step = 3
            output = b''
        elif step == 3 and b'# ' in output:
            time.sleep(0.3)
            break

os.close(master)
proc.terminate()
