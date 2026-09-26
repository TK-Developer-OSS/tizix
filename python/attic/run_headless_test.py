import os
import pty
import subprocess
import select
import time
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

TIZIX = tzpaths.TIZIX
wait_disk_ready = tzpaths.wait_disk_ready


def calc_crc(data):
    crc = 0
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

with open(os.path.join(TIZIX, 'full_test.log'), 'w') as logf:
    if not wait_disk_ready(tzpaths.DRIVEB):
        logf.write('run_headless_test: driveb.dsk not settled; launching anyway\n')
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        tzpaths.CPMSIM_CMD,
        cwd=tzpaths.CPMSIM_CWD,
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

    send_str("date\r\n")
    read_until(b'# ')

    send_str("rx TESTFILE.TXT\r\n")
    
    buf = b''
    t0 = time.time()
    found_c = False
    while time.time() - t0 < 5.0:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            c = os.read(master, 1)
            if not c:
                break
            logf.write(c.decode('latin-1', 'replace'))
            logf.flush()
            if c == b'C':
                found_c = True
                break

    if not found_c:
        logf.write("\nDid not receive 'C' from rx!\n")
        proc.terminate()
        sys.exit(1)

    payload = b"Hello XMODEM World from tizix rx test!\n" + b"A" * (128 - 39)
    crc = calc_crc(payload)
    pkt = bytes([0x01, 0x01, 0xFE]) + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    os.write(master, pkt)

    found_ack = False
    t0 = time.time()
    while time.time() - t0 < 3.0:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            c = os.read(master, 1)
            if c == b'\x06':
                found_ack = True
                logf.write("\nReceived ACK for packet 1\n")
                logf.flush()
                break

    if not found_ack:
        logf.write("\nDid not receive ACK for packet 1!\n")
        proc.terminate()
        sys.exit(1)

    os.write(master, bytes([0x04]))

    found_ack2 = False
    t0 = time.time()
    while time.time() - t0 < 3.0:
        r, _, _ = select.select([master], [], [], 0.1)
        if r:
            c = os.read(master, 1)
            if c == b'\x06':
                found_ack2 = True
                logf.write("\nReceived ACK for EOT\n")
                logf.flush()
                break

    read_until(b'# ')

    send_str("cat TESTFILE.TXT\r\n")
    out = read_until(b'# ')

    send_str("exit\r\n")
    time.sleep(0.5)

    os.close(master)
    try:
        proc.terminate()
    except:
        pass

    if b"Hello XMODEM World from tizix rx test!" in out:
        logf.write("\n\nTEST PASSED!\n")
    else:
        logf.write("\n\nTEST FAILED!\n")
