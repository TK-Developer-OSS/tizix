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
state = 0

# Test sequence:
# 0: wait for prompt -> send 'date'
# 1: wait for prompt -> send 'kill'
# 2: wait for prompt -> send 'exit'
# 3: wait a bit and finish

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
        
        if b'# ' in output:
            if state == 0:
                print("\n>>> [TEST] Sending 'date'")
                send_cmd('date')
                state = 1
                output = b''
            elif state == 1:
                print("\n>>> [TEST] Sending 'kill'")
                send_cmd('kill')
                state = 2
                output = b''
            elif state == 2:
                print("\n>>> [TEST] Sending 'exit'")
                send_cmd('exit')
                state = 3
                output = b''

with open(os.path.expanduser('~/z80pack/tizix/test_result.log'), 'w') as f:
    f.write("".join(full_log))

os.close(master)
proc.terminate()
