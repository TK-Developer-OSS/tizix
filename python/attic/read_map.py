# Let's inspect vi.map fully
import os
txt = open(os.path.expanduser('~/z80pack/tizix/user/vi.map')).read()
print(txt[:2000])
