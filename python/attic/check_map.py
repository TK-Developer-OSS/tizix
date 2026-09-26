# Let's inspect instructions in crt0cmd and _main before 0x0063
# In vi.asm:
# _start is at 0x0020
# _main is at 0x065b (or where?)
# Let's find addresses in vi.map
import os

lines = open(os.path.expanduser('~/z80pack/tizix/user/vi.map')).readlines()
for l in lines:
    if '_start' in l or '_main' in l or '_vt_clear' in l:
        print(l.strip())
