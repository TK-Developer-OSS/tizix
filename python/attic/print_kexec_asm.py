import os
lines = open(os.path.expanduser('~/z80pack/tizix/kexec.asm')).readlines()
for i in range(60, 230):
    print(f"{i+1:3d}: {lines[i]}", end='')
