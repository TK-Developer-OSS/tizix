import os
import sys

# Read DRIVER.BIN
drv = open(os.path.expanduser('~/z80pack/tizix/user/driver.bin'), 'rb').read()
print("DRIVER.BIN length:", len(drv))

# drv_tbl is at 0x9000
# drv_tbl[2] (printf) is at offset 4
printf_addr = drv[4] | (drv[5] << 8)
print(f"printf (drv_tbl[2]) addr in DRIVER: 0x{printf_addr:04x}")

# Let's inspect bytes at printf_addr (which is an address in 0x9000..0x9FFF range)
drv_offset = printf_addr - 0x9000
print(f"Bytes at printf (offset 0x{drv_offset:04x}):")
for i in range(drv_offset, drv_offset + 32):
    print(f"0x{0x9000+i:04x}: {drv[i]:02x}")
