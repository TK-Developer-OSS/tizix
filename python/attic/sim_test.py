# Simulate exact execution of vi.bin under Z80
# Load DRIVER.BIN at 0x9000
# Load vi.bin at 0xA000
# Relocate vi.bin
# Setup stack and registers as crt0cmd expects
import os

class Z80State:
    def __init__(self):
        self.mem = bytearray(0x10000)
        self.pc = 0xA020
        self.sp = 0xA000 + 0x1000 - 14 # simulated initial SP
        self.iy = 0xA000
        self.ix = 0
        self.hl = 0
        self.de = 0
        self.bc = 0
        self.af = 0

state = Z80State()
# Load driver
drv = open(os.path.expanduser('~/z80pack/tizix/user/driver.bin'), 'rb').read()
state.mem[0x9000:0x9000+len(drv)] = drv

# Load vi.bin
vi = open(os.path.expanduser('~/z80pack/tizix/user/vi.bin'), 'rb').read()
state.mem[0xA000:0xA000+len(vi)] = vi

# Relocate vi.bin
cnt = vi[0] | (vi[1] << 8)
tbl_off = vi[2] | (vi[3] << 8)
base = 0xA000
for i in range(cnt):
    off = vi[tbl_off + i*2] | (vi[tbl_off + i*2 + 1] << 8)
    val = state.mem[base + off] | (state.mem[base + off + 1] << 8)
    val = (val + base) & 0xFFFF
    state.mem[base + off] = val & 0xFF
    state.mem[base + off + 1] = (val >> 8) & 0xFF

print(f"Setup complete. Base=0xA000, reloc count={cnt}")
print(f"Initial PC=0x{state.pc:04x}")
