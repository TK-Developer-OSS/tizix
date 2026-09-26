import os
bin_ = open(os.path.expanduser('~/z80pack/tizix/user/vi.bin'), 'rb').read()
cnt = bin_[0] | (bin_[1] << 8)
tbl_off = bin_[2] | (bin_[3] << 8)
print('cnt:', cnt, 'tbl_off:', hex(tbl_off), 'total len:', len(bin_))
tbl = [bin_[tbl_off + i*2] | (bin_[tbl_off + i*2 + 1] << 8) for i in range(cnt)]
print('relocs near 0x0063:', [hex(x) for x in tbl if 0x0050 <= x <= 0x0080])
