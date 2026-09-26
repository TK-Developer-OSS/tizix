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
    stdin=slave, stdout=slave, stderr=slave, close_fds=True
)
os.close(slave)

def read_all(timeout=3.0):
    buf = b''
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            c = os.read(master, 1024)
            if not c: break
            buf += c
            sys.stdout.write(c.decode('latin-1', 'replace'))
            sys.stdout.flush()
    return buf

print("Waiting for boot...")
read_all(1.5)
for cmd in ['date', 'hello']:
    print(f'\n--- Sending {cmd} ---')
    os.write(master, f'{cmd}\r\n'.encode('latin-1'))
    read_all(2.0)

print('\n--- Exiting ---')
os.close(master)
proc.terminate()
