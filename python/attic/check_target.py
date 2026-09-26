import os
import sys

data = open(os.path.expanduser('~/z80pack/tizix/user/vi.raw.bin'), 'rb').read()
off = 0x0043
print('At 0x0043:', [hex(b) for b in data[off:off+10]])
