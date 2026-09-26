import os
import sys

raw = open(os.path.expanduser('~/z80pack/tizix/user/vi.raw.bin'), 'rb').read()
off = 0x006e # call ___sdcc_call_hl の飛び先アドレス格納位置
call_target = raw[off] | (raw[off+1] << 8)
print(f'raw call target = 0x{call_target:04x}')
