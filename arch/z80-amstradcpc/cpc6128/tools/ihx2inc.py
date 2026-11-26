#!/usr/bin/env python3
"""ihx2inc.py IHX LABEL BASE MAXLEN : print the bytes of an Intel hex file (which must start at BASE) as .db lines,
preceded by LABEL: and followed by a LABEL_len symbol.  Refuses a gap or a length over MAXLEN."""
import sys

def read_ihx(path):
    mem = {}
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"): continue
        n, a, t = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
        if t == 0:
            for i in range(n): mem[a + i] = int(line[9 + 2 * i:11 + 2 * i], 16)
    return mem

if __name__ == "__main__":
    ihx, label, base, maxlen = sys.argv[1], sys.argv[2], int(sys.argv[3], 0), int(sys.argv[4], 0)
    mem = read_ihx(ihx)
    lo, hi = min(mem), max(mem)
    if lo != base: sys.exit("%s starts at 0x%04X, not 0x%04X" % (ihx, lo, base))
    if hi - lo + 1 != len(mem): sys.exit("%s has a gap" % ihx)
    n = len(mem)
    if n > maxlen: sys.exit("%s is %d bytes, over %d" % (ihx, n, maxlen))
    print("%s:" % label)
    for i in range(0, n, 8):
        print("        .db     " + ",".join("0x%02X" % mem[lo + j] for j in range(i, min(i + 8, n))))
    print("%s_len = %d" % (label.replace("_image", ""), n) if label.endswith("_image") else "%s_len = %d" % (label, n))
