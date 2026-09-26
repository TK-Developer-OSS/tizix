import os
bin_ = open(os.path.expanduser('~/z80pack/tizix/user/vi.bin'), 'rb').read()

print("Bytes at 0x0035..0x0055:")
for i in range(0x0035, 0x0055):
    print(f"0x{i:04x}: {bin_[i]:02x}")
