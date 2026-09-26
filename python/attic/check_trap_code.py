import os
f = open(os.path.expanduser('~/z80pack/z80pack-1.37/cpmsim/srcsim/simctl.c')).read()
for i, l in enumerate(f.splitlines()[120:140]):
    print(f"{121+i}: {l}")
