# Let's inspect sim4.c from line 350 to 400
import os
lines = open(os.path.expanduser('~/z80pack/z80pack-1.37/z80core/sim4.c')).readlines()
for i in range(350, 400):
    print(f"{i+1}: {lines[i]}", end='')
