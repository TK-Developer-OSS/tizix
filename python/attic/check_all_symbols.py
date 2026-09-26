# Let's inspect vi.map symbol addresses and data locations
import os
lines = open(os.path.expanduser('~/z80pack/tizix/user/vi.map')).readlines()
for l in lines:
    if '0000' in l:
        print(l.strip())
