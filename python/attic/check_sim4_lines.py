import os
f = open(os.path.expanduser('~/z80pack/z80pack-1.37/z80core/sim4.c')).read()
lines = f.splitlines()
for i, l in enumerate(lines[:150]):
    if 'op_ed' in l or 'trap' in l.lower():
        print(f"{i+1}: {l}")
