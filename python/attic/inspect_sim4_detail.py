# Let's inspect sim4.c where OPTRAP2 happens
import os
txt = open(os.path.expanduser('~/z80pack/z80pack-1.37/z80core/sim4.c')).read()
lines = txt.splitlines()
for i, l in enumerate(lines):
    if 'op_ldbcinn' in l:
        print(f"op_ldbcinn line {i+1}:")
        for j in range(max(0, i-5), min(len(lines), i+30)):
            print(f"{j+1:4d}: {lines[j]}")
        break
