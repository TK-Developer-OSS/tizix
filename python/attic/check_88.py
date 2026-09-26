import os
import sys

raw = open(os.path.expanduser('~/z80pack/tizix/user/vi.raw.bin'), 'rb').read()
off = 0x0088 # call target for vt_move's printf
print('At 0x0088 in raw:', [hex(b) for b in raw[off-15:off+20]])
