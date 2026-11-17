#!/usr/bin/env python3
"""upload.py - send the kernel and the disk image to tizix's UART loader (fpga/loader).
  On the board:  upload.py kernel.bin disk.img --port /dev/ttyUSB0 [--baud 115200]
  Only build the stream to send for simulation:  upload.py kernel.bin disk.img --emit stream.bin
  Loader protocol: 'W' addr(4) len(4) data xor(1) -> 'K'/'E', and finally 'G' (jump to 0x80000000).
  All-zero 1KB chunks are not sent (the loader fills the whole SDRAM with 0 at start-up)."""
import argparse, struct, sys, time

CHUNK = 1024
DISK_OFFSET = 0x200000      # DISK_BASE of plat.h minus 0x80000000

def chunks(kernel, disk):
    for base, img in ((0, kernel), (DISK_OFFSET, disk)):
        for off in range(0, len(img), CHUNK):
            d = img[off:off + CHUNK]
            if any(d):
                yield base + off, d

def frame(addr, d):
    x = 0
    for b in d:
        x ^= b
    return b'W' + struct.pack('<II', addr, len(d)) + d + bytes([x])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('kernel'); ap.add_argument('disk')
    ap.add_argument('--port'); ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--emit')
    ap.add_argument('--no-go', action='store_true')
    a = ap.parse_args()
    kernel = open(a.kernel, 'rb').read()
    disk = open(a.disk, 'rb').read()
    if len(kernel) > DISK_OFFSET:
        sys.exit("kernel is larger than the disk offset")
    frames = [frame(ad, d) for ad, d in chunks(kernel, disk)]
    go = b'' if a.no_go else b'G'
    total = sum(len(f) for f in frames)
    if a.emit:
        open(a.emit, 'wb').write(b''.join(frames) + go)
        print("%d frames, %d bytes -> %s" % (len(frames), total, a.emit))
        return
    import serial
    s = serial.Serial(a.port, a.baud, timeout=1)
    print("waiting for TZLD (reset the board; the loader clears SDRAM first)...")
    buf = b''
    while b'TZLD' not in buf:
        buf += s.read(64)
    t0 = time.time()
    for i, f in enumerate(frames):
        for retry in range(3):
            s.write(f)
            r = s.read(1)
            if r == b'K':
                break
        else:
            sys.exit("frame %d failed" % i)
        if i % 32 == 0:
            print("\r%d/%d" % (i, len(frames)), end='', flush=True)
    print("\ndone in %.0f s" % (time.time() - t0))
    if go:
        s.write(go)

main()
