import os
import sys

bin_ = open(os.path.expanduser('~/z80pack/tizix/user/vi.bin'), 'rb').read()
cnt = bin_[0] | (bin_[1] << 8)
tbl_off = bin_[2] | (bin_[3] << 8)

mem = bytearray(bin_)
if len(mem) < 0x1000:
    mem.extend(b'\x00' * (0x1000 - len(mem)))

base = 0xA000
for i in range(cnt):
    off = bin_[tbl_off + i*2] | (bin_[tbl_off + i*2 + 1] << 8)
    val = mem[off] | (mem[off+1] << 8)
    val = (val + base) & 0xFFFF
    mem[off] = val & 0xFF
    mem[off+1] = (val >> 8) & 0xFF

print("Code leading up to 0x0088 in block:")
for a in range(0x0065, 0x0095):
    print(f"{0xA000+a:04x}: {mem[a]:02x}")
