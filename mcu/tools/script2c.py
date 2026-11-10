#!/usr/bin/env python3
"""script2c.py FILE... -> C source holding each FILE as a NUL-terminated string in flash, and the
table tz_scripts[] { name, text } (name = the file name, extension included), ended by { 0, 0 }."""
import os
import re
import sys

files = sys.argv[1:]
print('#include "mcu.h"')
names = []
for path in files:
    name = os.path.basename(path)
    ident = re.sub("[^A-Za-z0-9]", "_", name)
    names.append((name, ident))
    data = open(path, "rb").read()
    print("static const char t_%s[] MPROGMEM =" % ident)
    for line in data.split(b"\n")[:-1] + ([data.rsplit(b"\n", 1)[-1]] if not data.endswith(b"\n") else []):
        s = "".join(chr(c) if 32 <= c < 127 and chr(c) not in '"\\?' else "\\%03o" % c for c in line)
        print('    "%s\\n"' % s)
    print(";")
    print("static const char n_%s[] MPROGMEM = \"%s\";" % (ident, name))
print("const struct tz_script tz_scripts[] MPROGMEM = {")
for name, ident in names:
    print("    { n_%s, t_%s }," % (ident, ident))
print("    { 0, 0 }")
print("};")
