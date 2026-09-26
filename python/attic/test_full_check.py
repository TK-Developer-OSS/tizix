import os
import pty
import subprocess
import select
import time
import sys

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

def test_full():
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

    full_output = b''

    def read_until(pattern, timeout=10.0):
        nonlocal full_output
        t0 = time.time()
        buf = b''
        while time.time() - t0 < timeout:
            r, _, _ = select.select([master], [], [], 0.05)
            if r:
                try:
                    c = os.read(master, 1024)
                    if not c:
                        break
                    buf += c
                    sys.stdout.write(c.decode('latin-1', 'replace'))
                    sys.stdout.flush()
                    if pattern in buf:
                        return buf
                except OSError:
                    break
        return buf

    def send_str(s):
        time.sleep(0.3)
        os.write(master, s.encode('latin-1'))

    print(">>> 1. Waiting for prompt '# '")
    out = read_until(b'# ', timeout=5.0)
    print(f"\n[DEBUG] Boot output:\n{out.decode('latin-1', 'replace')}")

    print("\n>>> 2. Testing 'date' command")
    full_output = b''
    send_str("date\r\n")
    read_until(b'# ')

    print("\n>>> 3. Testing 'rx TESTFILE.TXT' command")
    full_output = b''
    send_str("rx TESTFILE.TXT\r\n")
    
    # Wait for XMODEM CRC 'C'
    t0 = time.time()
    found_c = False
    while time.time() - t0 < 5.0:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            c = os.read(master, 1)
            if not c:
                break
            sys.stdout.write(c.decode('latin-1', 'replace'))
            sys.stdout.flush()
            if c == b'C':
                found_c = True
                break

    if not found_c:
        print("\nDid not receive 'C' from rx!")
        proc.terminate()
        return False

    print("\n>>> 4. Sending XMODEM packet 1")
    payload = b"Hello XMODEM World from tizix rx test!\n" + b"A" * (128 - 39)
    crc = calc_crc(payload)
    pkt = bytes([0x01, 0x01, 0xFE]) + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    time.sleep(0.1)
    os.write(master, pkt)

    # Wait for ACK (0x06)
    t0 = time.time()
    found_ack = False
    while time.time() - t0 < 3.0:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            c = os.read(master, 1)
            if c == b'\x06':
                found_ack = True
                print("\nReceived ACK for packet 1")
                break

    if not found_ack:
        print("\nDid not receive ACK for packet 1!")
        proc.terminate()
        return False

    print("\n>>> 5. Sending EOT (0x04)")
    time.sleep(0.1)
    os.write(master, bytes([0x04]))

    # Wait for ACK
    found_ack2 = False
    t0 = time.time()
    while time.time() - t0 < 3.0:
        r, _, _ = select.select([master], [], [], 0.05)
        if r:
            c = os.read(master, 1)
            if c == b'\x06':
                found_ack2 = True
                print("\nReceived ACK for EOT")
                break

    full_output = b''
    read_until(b'# ')

    print("\n>>> 6. Testing 'cat TESTFILE.TXT'")
    full_output = b''
    send_str("cat TESTFILE.TXT\r\n")
    out = read_until(b'# ')

    print("\n>>> 7. Exiting")
    send_str("exit\r\n")
    time.sleep(0.5)

    os.close(master)
    try:
        proc.terminate()
    except:
        pass

    if b"Hello XMODEM World from tizix rx test!" in out:
        print("\n\nSUCCESS! All tests passed perfectly.")
        return True
    else:
        print("\n\nFailed to verify file contents via cat.")
        return False

if __name__ == '__main__':
    ok = test_full()
    with open(os.path.expanduser('~/z80pack/tizix/full_test_result.txt'), 'w') as f:
        f.write("PASS" if ok else "FAIL")
