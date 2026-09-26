import os
txt = open(os.path.expanduser('~/z80pack/z80pack-1.37/cpmsim/srcsim/simctl.c')).read()
for i, line in enumerate(txt.splitlines()):
    if 'trap' in line.lower() or 'op-code' in line.lower() or 'optrap' in line.lower():
        print(f"{i+1}: {line}")
