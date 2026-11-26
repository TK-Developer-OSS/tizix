#!/usr/bin/env python3
"""test_z80_amstradcpc.py - end-to-end test of arch/z80-amstradcpc/cpc6128 on MAME (cpc6128, no window).
  make ARCH=z80-amstradcpc builds disks/cpc.dsk; MAME boots it with |CPM, python/test_z80_amstradcpc.lua types the lines
  through MAME's natural keyboard and prints the screen as text from the console's shadow buffer (80x25 bytes at 0xF400).
  Needs the cpc6128 ROMs (cpc6128.rom, cpcados.rom) on MAME's rompath, e.g. ~/mame/roms/cpc6128/ (Caprice32 ships them:
  its rom/amsdos.rom is cpcados.rom).  MAME = the binary (default mame).  CPC_SNAP=DIR: also save a PNG snapshot of the
  last screen in DIR.  Usage: python3 python/test_z80_amstradcpc.py"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAME = os.environ.get("MAME", "mame")
DSK = os.path.join(ROOT, "arch/z80-amstradcpc/cpc6128/disks/cpc.dsk")
subprocess.check_call(["make", "-s", "ARCH=z80-amstradcpc", "DEVICE=cpc6128"], cwd=ROOT,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
tests = [
    ("echo abc | cat",        "abc"),
    ("ls /bin | wc",          None),
    ("echo hi > /root/t.txt", None),
    ("cat /root/t.txt",       "hi"),
    ("cat /etc/rc",           "rsyslog"),
    ("echo a&b {x} \"q\"",     "a&b {x} q"),
    ("ls /bin",               "vi10.ovl"),            # 66 lines: the screen scrolls
    ("uptime",                "up "),
]
tmp = tempfile.mkdtemp()
try:
    shutil.copy(DSK, tmp + "/cpc.dsk")                  # the disk is written to; keep the built image as it is
    with open(tmp + "/lines", "w") as f:
        f.write("".join(l + "\n" for l, _ in tests))
    env = dict(os.environ, TZ_LINES=tmp + "/lines")
    snap = ["-snapshot_directory", os.environ["CPC_SNAP"]] if os.environ.get("CPC_SNAP") else []
    if snap: env["TZ_SNAP"] = "1"
    out = subprocess.run([MAME, "cpc6128", "-flop1", tmp + "/cpc.dsk", "-video", "none", "-sound", "none", "-nothrottle"] + snap + [
                          "-skip_gameinfo", "-autoboot_script", os.path.join(ROOT, "python/test_z80_amstradcpc.lua")],
                         env=env, capture_output=True, text=True, timeout=600).stdout
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
for i, (line, want) in enumerate(tests, 1):
    full = screens.get(str(i))
    if full is None:
        print("FAIL  %s  (no screen)" % line); fail += 1; continue
    # the output of this line: what follows the last line that shows it typed (the whole screen when it has scrolled off)
    at = max([j for j, l in enumerate(full) if l.endswith(line)] or [-1])
    res = "\n".join(full[at + 1:-1])
    if os.environ.get("CPC_V"): print("\n".join(full))
    ok = "EXC" not in res and (want is None or want in res)
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print("\n".join(full[-8:]))
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
