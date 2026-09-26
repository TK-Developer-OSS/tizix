import os
import glob
for fn in glob.glob(os.path.expanduser('~/z80pack/z80pack-1.37/z80core/sim*.c')):
    for i, line in enumerate(open(fn).readlines()):
        if 'cpu_error = OPTRAP2' in line:
            print(f"{fn}:{i+1}: {line.strip()}")
