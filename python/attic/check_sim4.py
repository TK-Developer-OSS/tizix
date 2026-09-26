# Let's inspect sim4.c in z80pack
# where trap_ed is called
import os
import glob

f = open(os.path.expanduser('~/z80pack/z80pack-1.37/z80core/sim4.c')).read()
# Let's find the dispatch table for ED prefix
print("Searching ED opcode 0x4b in sim4.c:")
for l in f.splitlines():
    if '0x4b' in l or 'op_ldbcinn' in l or 'op_lddeinn' in l:
        print(l)
