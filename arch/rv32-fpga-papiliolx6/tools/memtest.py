#!/usr/bin/env python3
"""memtest.py - write the SDRAM with the loader's 'W'/'R' (fpga/loader), read it back and count the corrupted bytes.
  Usage: memtest.py --port /dev/ttyUSB1   (run right after the loader prints TZLD, before sending 'G')
  It writes a pattern 1KB at a time, reads it back and compares. For telling phase problems from pin problems."""
import argparse, random, struct, sys, serial

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True); ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--cap', type=int)
    ap.add_argument('--addrs', default='0,0x1000,0x100000,0x200000,0x7FFC00')
    a = ap.parse_args()
    s = serial.Serial(a.port, a.baud, timeout=2)
    if a.cap is not None:
        s.write(b'C' + bytes([a.cap])); print('cap', a.cap, s.read(1))
    random.seed(1)
    bad = 0
    for ad in [int(x, 0) for x in a.addrs.split(',')]:
        d = bytes(random.randrange(256) for _ in range(1024))
        x = 0
        for b in d:
            x ^= b
        s.write(b'W' + struct.pack('<II', ad, 1024) + d + bytes([x]))
        r = s.read(1)
        s.write(b'R' + struct.pack('<II', ad, 1024))
        back = s.read(1025)
        diff = [i for i in range(1024) if len(back) > i and back[i] != d[i]]
        print("%08x W->%r  read %d bytes, %d differ%s" % (ad, r, len(back), len(diff),
              ("  first at %d: want %02x got %02x" % (diff[0], d[diff[0]], back[diff[0]])) if diff else ""))
        bad += len(diff) + (1 if len(back) < 1025 else 0)
    sys.exit(1 if bad else 0)

main()
