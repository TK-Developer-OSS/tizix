#!/usr/bin/env python3
"""test_z80_ti84plus.py - end-to-end test of arch/z80-ti84plus (TI-84 Plus on the z80e emulator).
  make ARCH=z80-ti84plus builds rom.bin (kernel + FAT image in flash), tizsim (arch/z80-ti84plus/tools, a small
  frontend of libz80e) boots it, the commands go in through the link assist and the output is checked.
  tizsim: set TIZSIM=/path/to/tizsim (default /mnt/sda1/ti84/tizsim; see arch/z80-ti84plus/tools/README.md)
  Usage: python3 python/test_z80_ti84plus.py"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARCH = os.path.join(ROOT, "arch/z80-ti84plus")
TIZSIM = os.environ.get("TIZSIM", "/mnt/sda1/ti84/tizsim")
subprocess.check_call(["make", "-s", "ARCH=z80-ti84plus"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

tests = [
    ("echo hello",             "hello"),
    ("echo abc | cat",         "abc"),
    ("ls /bin | wc",           "67"),
    ("echo hi > /root/t.txt",  None),
    ("cat /root/t.txt",        "hi"),
    ("uptime",                 "up "),
    ("cat /etc/rc",            "rsyslog boot-ok"),
]
inp = "".join(t + "\r" for t, _ in tests).encode()
out = subprocess.run([TIZSIM, os.path.join(ARCH, "rom.bin")], input=inp, stdout=subprocess.PIPE, timeout=900).stdout.decode(errors="replace")
# split the transcript at the prompts: "[/root]# <command>\n<output>"
parts = out.split("[/root]# ")
fail = 0
for line, want in tests:
    seg = next((p for p in parts if p.startswith(line)), None)
    ok = seg is not None and "EXC" not in seg and (want is None or want in seg[len(line):])
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print(seg if seg is not None else out[-400:])
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
