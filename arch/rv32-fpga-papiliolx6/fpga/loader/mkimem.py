#!/usr/bin/env python3
"""mkimem.py loader.bin out.vhd - make NEORV32's neorv32_imem_image package (the initial contents when IMEM is a ROM)."""
import sys
b = open(sys.argv[1], 'rb').read()
b += b'\0' * (-len(b) % 4)
words = [int.from_bytes(b[i:i+4], 'little') for i in range(0, len(b), 4)]
n = max(256, 1 << (len(words) - 1).bit_length())
with open(sys.argv[2], 'w') as f:
    f.write("library ieee;\nuse ieee.std_logic_1164.all;\n\npackage neorv32_imem_image is\n\n")
    f.write("type rom_t is array (0 to %d) of std_ulogic_vector(31 downto 0);\n" % (n - 1))
    f.write("constant image_size_c : natural := %d;\nconstant image_data_c : rom_t := (\n" % len(b))
    for w in words:
        f.write('x"%08x",\n' % w)
    f.write("others => (others => '0')\n);\n\nend package;\n")
