import os
import pty
import subprocess
import select
import time
import sys

with open(os.path.expanduser('~/z80pack/tizix/test_driver_libc.log'), 'w') as logf:
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ['./cpmsim', '-d', 'disks'],
        cwd=os.path.expanduser('~/z80pack/tizix/arch/z80pack'),
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True
    )
    os.close(slave)
    time.sleep(0.5)

    def read_until(pattern, timeout=5.0):
        buf = b''
        t0 = time.time()
        while time.time() - t0 < timeout:
            r, _, _ = select.select([master], [], [], 0.1)
            if r:
                c = os.read(master, 1024)
                if not c:
                    break
                buf += c
                logf.write(c.decode('latin-1', 'replace'))
                logf.flush()
                if pattern in buf:
                    return buf
        return buf

    def send_str(s):
        time.sleep(0.1)
        os.write(master, s.encode('latin-1'))

    out = read_until(b'# ')
    if b'FAT Drive ...... DETECTED' not in out:
        logf.write("\nFAT Drive failed to detect!\n")
        proc.terminate()
        sys.exit(1)

    send_str("test1\r\n")
    out = read_until(b'=== Test Complete ===', timeout=5.0)
    read_until(b'# ')

    send_str("exit\r\n")
    time.sleep(0.5)

    os.close(master)
    try:
        proc.terminate()
    except:
        pass

    if b"=== Test Complete ===" in out and b"strcat: Hello World!" in out and b"abs: abs(-42)=42" in out and b"dual fopen & fwrite ok" in out and b"fgetc: F" in out:
        logf.write("\n\nALL STDIO, STRING & STDLIB TESTS PASSED!\n")
        print("ALL STDIO, STRING & STDLIB TESTS PASSED!")
    else:
        logf.write("\n\nTESTS FAILED!\n")
        print("TESTS FAILED!")
        sys.exit(1)
