import os
import sys, re

asm = open(os.path.expanduser('~/z80pack/tizix/user/vi.asm')).read()
matches = re.findall(r'(\w+):\n\s+push\thl\n\s+ret', asm)
print("push hl / ret matches:", matches)

matches2 = re.findall(r'push\thl\n\s+ret', asm)
print("count of push hl / ret:", len(matches2))
