#!/usr/bin/env python3
"""test_rv32_fpga_sim.py - end-to-end test of arch/rv32-fpga-papiliolx6 (NEORV32 converted to Verilog and run on Verilator).
  Build simv first with arch/rv32-fpga-papiliolx6/fpga/build_sim.sh (it writes to /mnt/sda1/fpga/tizix-sim).
  The input is the commands plus CR and the waits (0xFF = 200ms) in one stream; the expected strings are looked for in the whole output.
  Characters are sent back to back (GAP=0. UART receive is interrupt driven with a 256-byte buffer. GAP=ns spaces the characters out).
  Usage: python3 python/test_rv32_fpga_sim.py"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARCH = "rv32-fpga-papiliolx6"
HEX = os.path.join(ROOT, "build/arch", ARCH, "obj/ram.hex")
SIMV = os.environ.get("SIMV", "/mnt/sda1/fpga/tizix-sim/vobj/simv")
subprocess.check_call(["make", "-s", "ARCH=" + ARCH], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# (input, expected string, wait (number of 200ms units)). Every access goes through the SDRAM (a dozen or so clocks each), so the waits are generous
SLOW = int(os.environ.get("SLOW", "3"))
tests = [
    ("hello",                  "Hello from C main!", 3),
    ("echo abc | cat",         "abc", 4),
    ("sleep 2",                None, 14),
    ("uptime",                 "up ", 3),
    ("echo hi > /root/t.txt",  None, 3),
    ("cat /root/t.txt",        "hi", 3),
    ("ls /bin | wc",           "38", 4),
    ("tzsh -c 'echo tzsh ok'", "tzsh ok", 5),
]
data = b""
for line, _, wait in tests:
    data += line.encode() + b"\r" + b"\xff" * (wait * SLOW // 2)
infile = "/tmp/rv32_fpga_in.txt"
open(infile, "wb").write(data)
ms = 300 + 200 * sum(w * SLOW // 2 for _, _, w in tests) + 1500
r = subprocess.run([SIMV, "+HEX=" + HEX, "+IN=" + infile, "+MS=%d" % ms, "+GAP=%s" % os.environ.get("GAP", "0")], capture_output=True, timeout=3600)
out = r.stdout.decode(errors="replace")
fail = 0
for line, want, _ in tests:
    ok = (line in out) and (want is None or want in out)
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    fail += not ok
if "sdram errors=0" not in out or "[sdram]" in out:
    print("FAIL  SDRAM protocol check"); fail += 1
if "EXC" in out:
    print("FAIL  EXC in output"); fail += 1
if fail:
    print(out)
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
