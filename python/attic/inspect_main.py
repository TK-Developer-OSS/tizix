# Let's see what happens after main is called:
# main in vi.c:
# void main(int argc, char **argv) {
#     init_screen();
#     ...
# }
import os

# Let's find _main in vi.asm:
open(os.path.expanduser('~/z80pack/tizix/user/vi.asm'))
# search _main:
lines = open(os.path.expanduser('~/z80pack/tizix/user/vi.asm')).readlines()
for i, l in enumerate(lines):
    if '_main:' in l:
        print(f"Line {i}:")
        for j in range(max(0, i-5), min(len(lines), i+40)):
            print(f"{j+1:4d}: {lines[j]}", end='')
        break
