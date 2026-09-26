import pty, os, time, sys

os.chdir(os.path.expanduser('~/z80pack/tizix/arch/z80pack'))  # cpmsim + disks はここへ移動

cmd = sys.argv[1] if len(sys.argv) > 1 else 'hello'

master, slave = pty.openpty()
pid = os.fork()
if pid == 0:
    os.close(master)
    os.dup2(slave, 0)
    os.dup2(slave, 1)
    os.dup2(slave, 2)
    os.close(slave)
    os.execlp('./cpmsim', './cpmsim')

os.close(slave)
time.sleep(0.5)
os.write(master, (cmd + '\n').encode('ascii'))
time.sleep(1.0)
os.write(master, b'exit\n')
time.sleep(0.5)

buf = b''
for _ in range(50):
    try:
        data = os.read(master, 1024)
        if not data: break
        buf += data
    except OSError:
        break
    time.sleep(0.05)

print(buf.decode('latin1', errors='ignore'))
