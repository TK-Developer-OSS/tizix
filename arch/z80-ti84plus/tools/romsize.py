#!/usr/bin/env python3
"""romsize.py kernel.map - print how much of the 32KB kernel image (flash pages 0-1) is used; exit 1 when it overflows."""
import re, sys
m = 0
for l in open(sys.argv[1]):
    g = re.match(r'\s*_(CODE|HOME|GSINIT|GSFINAL|INITIALIZER)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s*=', l)
    if g:
        m = max(m, int(g.group(2), 16) + int(g.group(3), 16))
print('ROM image end 0x%04X / 0x8000  (free %d B)' % (m, 0x8000 - m))
sys.exit(1 if m > 0x8000 else 0)
