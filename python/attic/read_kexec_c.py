# Let's inspect bytes in kexec.c before and after reloc
import os
txt = open(os.path.expanduser('~/z80pack/tizix/kexec.c')).read()
print(txt)
