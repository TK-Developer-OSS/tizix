# Let's inspect test1.c and compare with vi.c
import os
txt = open(os.path.expanduser('~/z80pack/tizix/user/test1.c')).read()
print(txt[:1000])
