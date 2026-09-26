import os
import sys

mem = bytearray(open(os.path.expanduser('~/z80pack/tizix/user/vi.bin'), 'rb').read())
cnt = mem[0] | (mem[1] << 8)
tbl_off = mem[2] | (mem[3] << 8)

if len(mem) < 0x1000:
    mem.extend(b'\x00' * (0x1000 - len(mem)))

base = 0xA000
for i in range(cnt):
    off = mem[tbl_off + i*2] | (mem[tbl_off + i*2 + 1] << 8)
    val = mem[off] | (mem[off+1] << 8)
    val = (val + base) & 0xFFFF
    mem[off] = val & 0xFF
    mem[off+1] = (val >> 8) & 0xFF

print(f"Memory at 0x0080..0x0090 (relocated):")
for a in range(0x0080, 0x0090):
    print(f"0x{a:04x} (PC=0x{base+a:04x}): {mem[a]:02x}")
