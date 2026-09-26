# Let's inspect kexec.c and see how kernel was compiled and what happens during kexec
# Let's compile a standalone test of kexec loop or disassemble kernel.bin
import os
import subprocess

# Let's check kernel.map for _kexec_file
lines = open(os.path.expanduser('~/z80pack/tizix/kernel.map')).readlines()
for l in lines:
    if '_kexec_file' in l:
        print(l.strip())
