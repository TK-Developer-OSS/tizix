#!/usr/bin/env python3
"""test_z80_zxspectrum.py - end-to-end test of arch/z80-zxspectrum/plus3 on MAME (specpls3, no window).
  make ARCH=z80-zxspectrum builds disks/disk3.dsk; arch/z80-zxspectrum/plus3/tools/mamezx.lua boots it, types each line
  with the same key translation as `make run` (the host characters as the Spectrum keys that give them in tizix, each
  held until tizix has scanned it) and prints the screen as text from the console's shadow buffer (32x24 at 0xF400).
  Needs the +3 ROMs (40092.ic7, 40093.ic8) on MAME's rompath, e.g. ~/mame/roms/specpls3/ (ZEsarUX's p2a40.rom is the
  two of them).  MAME = the binary (default mame).  ZX_V=1 prints every screen.  MAME runs at nice 19.
  Usage: python3 python/test_z80_zxspectrum.py"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAME = os.environ.get("MAME", "mame")
DSK = os.path.join(ROOT, "arch/z80-zxspectrum/plus3/disks/disk3.dsk")
LUA = os.path.join(ROOT, "arch/z80-zxspectrum/plus3/tools/mamezx.lua")
subprocess.check_call(["make", "-s", "ARCH=z80-zxspectrum", "DEVICE=plus3"], cwd=ROOT,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
# (typed line, text the output must contain or None, the line as it shows on the screen when it differs)
# \e = Esc, \r = Enter, \w = wait 3 s, \L = left arrow
tests = [
    ("echo abc | cat",                   "abc",         None),
    ("ls /bin | wc",                     None,          None),
    ("echo hi > /root/t.txt",            None,          None),
    ("cat /root/t.txt",                  "hi",          None),
    ("cat /etc/rc",                      "rsyslog",     None),
    ("uptime",                           "up ",         None),
    ('echo a&b {x} "q"',                 "a&b {x} q",   None),
    ("echo X-Y_z:~/@[]^\;=+*'",         "X-Y_z:~/@[]^\;=+*'", None),
    ("echo ?!#$%()`",                    "?!#$%()`",    None),
    ("echo ab\\LX",                      "aXb",         "echo aXb"),
    ("vi /root/v.txt\\r\\w\\w\\w\\wihello\\e:wq", None,   "vi /root/v.txt"),
    ("cat /root/v.txt",                  "hello",       None),
    ("sleep 5 &",                        None,          None),   # last: a background job holds a block (vi needs 3)
    ("ps",                               "sleep",       None),
]
tmp = tempfile.mkdtemp()
try:
    shutil.copy(DSK, tmp + "/disk3.dsk")               # the disk is written to; keep the built image as it is
    with open(tmp + "/lines", "w") as f:
        f.write("".join(l + "\n" for l, _, _ in tests))
    env = dict(os.environ, TZ_LINES=tmp + "/lines", TZ_ZXBOOT=tmp + "/disk3.dsk")
    for k in ("DISPLAY", "WAYLAND_DISPLAY"): env.pop(k, None)   # with a display MAME (SDL) opens an empty full-screen window, even with -video none
    out = subprocess.run(["nice", "-n", "19", "ionice", "-c3", MAME, "specpls3", "-flop", tmp + "/disk3.dsk",
                          "-video", "none", "-sound", "none", "-nothrottle", "-skip_gameinfo", "-autoboot_script", LUA],
                         env=env, capture_output=True, text=True, timeout=900).stdout
finally:
    shutil.rmtree(tmp)
screens, cur = {}, None
for l in out.splitlines():
    if l.startswith("@@SCREEN "): cur = l[9:]; screens[cur] = []
    elif l == "@@END": cur = None
    elif cur is not None: screens[cur].append(l)
if "timeout" in screens or "0" not in screens:
    print("\n".join(screens.get("timeout", [])) or out)
    sys.exit("*** no prompt (timeout) ***")
print("boot screen tail:", repr("\n".join(screens["0"])[-80:]))
fail = 0
for i, (line, want, shown) in enumerate(tests, 1):
    full = screens.get(str(i))
    if full is None:
        print("FAIL  %s  (no screen)" % line); fail += 1; continue
    # the output: what follows the last screen line showing the command (32 columns: it may wrap; then the whole screen)
    text = "\n".join(full)
    key = (shown or line)[:20]
    at = text.rfind(key)
    res = text[text.find("\n", at) + 1:] if at >= 0 else text
    res = res[:res.rfind("[/root]#")]
    if os.environ.get("ZX_V"): print(text)
    ok = "EXC" not in res and (want is None or want in res.replace("\n", ""))
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print(text)
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
