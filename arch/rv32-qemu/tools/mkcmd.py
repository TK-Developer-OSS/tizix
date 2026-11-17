#!/usr/bin/env python3
"""mkcmd.py A.bin B.bin BASE_A BASE_B OUT.bin
From two images of the same command linked at BASE_A and BASE_B, make a .bin with a relocation table.
A word (4 bytes) that differs by (BASE_B - BASE_A) is an address, one that is the same is a constant. Anything else is an error
(an instruction that is not PC-relative = a wrong compile flag). The layout is at the top of loader.c."""
import struct, sys

a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
ba, bb = int(sys.argv[3], 0), int(sys.argv[4], 0)
if len(a) != len(b) or len(a) % 4:
    sys.exit("mkcmd: image sizes differ or not multiple of 4")
d = (bb - ba) & 0xFFFFFFFF
rel = []
for i in range(0, len(a), 4):
    wa, wb = struct.unpack_from('<I', a, i)[0], struct.unpack_from('<I', b, i)[0]
    if wa == wb:
        continue
    if (wb - wa) & 0xFFFFFFFF == d:
        rel.append(i // 4)
    else:
        sys.exit("mkcmd: word %d differs (%08x vs %08x): not a plain address" % (i // 4, wa, wb))
if len(rel) and max(rel) > 0xFFFF:
    sys.exit("mkcmd: too large")
out = struct.pack('<IIII', 0x31525A54, len(a), len(rel), ba) + a
out += b''.join(struct.pack('<H', r) for r in rel)
open(sys.argv[5], 'wb').write(out)
