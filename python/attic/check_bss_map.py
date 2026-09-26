import os
lines = open(os.path.expanduser('~/z80pack/tizix/user/vi.map')).readlines()
for l in lines:
    if '_num_lines' in l or '_line_buf' in l or '_cur_filename' in l or '_cursor_' in l:
        print(l.strip())
