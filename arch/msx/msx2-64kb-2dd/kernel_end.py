#!/usr/bin/env python3
"""kernel_end.py MAP NSEC - check the kernel image fits the boot loader's read size.

The image is read from the disk as NSEC * 128 bytes starting at 0x0100.  The end is the
highest end of the _CODE/_HOME/_GSINIT/_GSFINAL/_INITIALIZER areas in the linker map."""
import re, sys

end = 0
for line in open(sys.argv[1]):
    m = re.match(r'_(CODE|HOME|GSINIT|GSFINAL|INITIALIZER)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s*=', line)
    if m:
        end = max(end, int(m.group(2), 16) + int(m.group(3), 16))
limit = 256 + int(sys.argv[2]) * 128
if end > limit:
    sys.exit("*** kernel image end 0x%04X > load limit 0x%04X (NSEC=%s). raise NSEC or shrink the code ***"
             % (end, limit, sys.argv[2]))
print("kernel image end 0x%04X / load limit 0x%04X  (free %d B)" % (end, limit, limit - end))
