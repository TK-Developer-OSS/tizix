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

def send_cmd(cmd):
    time.sleep(0.4)
    os.write(master, (cmd + '\r\n').encode())

output = b''
full_log = []
t0 = time.time()
commands = ['cp_dbg HELLO.BIN HELLO2.BIN', 'exit']
cmd_idx = 0

while time.time() - t0 < 8:
    r, _, _ = select.select([master], [], [], 0.2)
    if r:
        try:
            chunk = os.read(master, 1024)
            if not chunk:
                break
            output += chunk
            text = chunk.decode('latin-1', 'replace')
            sys.stdout.write(text)
            sys.stdout.flush()
            full_log.append(text)
        except OSError:
            break
        
        if b'# ' in output and cmd_idx < len(commands):
            cmd = commands[cmd_idx]
            print(f"\n>>> [TEST] Sending '{cmd}'")
            send_cmd(cmd)
            cmd_idx += 1
            output = b''

os.close(master)
try:
    proc.terminate()
except:
    pass
