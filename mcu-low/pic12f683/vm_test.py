#!/usr/bin/env python3
"""Run vm.asm in gpsim with a bytecode program in its EEPROM and watch GP4.
usage: vm_test.py        (needs gpasm and gpsim)"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vm"))
from nodevm import assemble

os.chdir(os.path.dirname(os.path.abspath(__file__)))
prog = assemble(open("../vm/examples/blink.s").read())
open("prog.inc", "w").write("".join("        de %s\n" % ",".join("0x%02X" % b for b in prog[i:i + 8]) for i in range(0, len(prog), 8)))
subprocess.run(["gpasm", "-p", "p12f683", "-o", "vm.hex", "vm.asm"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def image(prog):
    open("prog.inc", "w").write("".join("        de %s\n" % ",".join("0x%02X" % b for b in prog[i:i + 8]) for i in range(0, len(prog), 8)))
    subprocess.run(["gpasm", "-p", "p12f683", "-o", "vm.hex", "vm.asm"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def sample(cycles, regs):
    """run to each cycle count in turn and read the registers (gpio, or RAM addresses); [(cycle, {reg: value})]"""
    stc = "load s vm.cod\nstimulus asynchronous_stimulus\ninitial_state 1\nstart_cycle 0\ndigital\n{ 0,1 }\nname idle\nend\nnode n_line\nattach n_line idle p12f683.gpio2\n"
    for c in cycles:
        stc += "break c %d\nrun\n" % c
        for r in regs: stc += "x %s\n" % r
    stc += "quit\n"
    open("vm_run.stc", "w").write(stc)
    out = subprocess.run(["gpsim", "-i", "-c", "vm_run.stc"], check=True, timeout=600, capture_output=True, text=True).stdout
    vals = [int(m.group(1), 16) for m in re.finditer(r"\[0x[0-9a-f]+\] = 0x([0-9a-f]+)", out)]
    return [vals[i * len(regs):(i + 1) * len(regs)] for i in range(len(cycles))]

fails = 0
def check(name, cond, got=None):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
    fails += not cond

def blink_edges(prog, label):
    EDGE, SLOP = 500000, 3000          # the pin flips every 500000 cycles (4 MHz, 1 us per cycle)
    cyc = []
    for k in range(1, 5): cyc += [EDGE * k - SLOP, EDGE * k + SLOP]
    vals = [(v[0] >> 4) & 1 for v in sample(cyc, ["gpio"])]
    check("%s: sampled %d points" % (label, len(cyc)), len(vals) == len(cyc), vals)
    for k in range(1, 5):
        if 2 * k <= len(vals):
            b, a = vals[2 * k - 2], vals[2 * k - 1]
            check("%s: edge %d at ~%d ms: GP4 %d -> %d" % (label, k, k * 500, b, a), b == (k - 1) % 2 and a == k % 2, (b, a))

# 1. the hand written blink (17 bytes)
prog = assemble(open("../vm/examples/blink.s").read()); image(prog)
blink_edges(prog, "blink.s")

# 2. the same blink from tzsh, 51 bytes, with a loop that counts and prints a message
from tzsh2bc import compile_
prog, _ = compile_(open("../vm/examples/blink.sh").read()); image(prog)
blink_edges(prog, "blink.sh")
ring = sample([2500000], ["0x%x" % (0x50 + i) for i in range(16)])[0]
text = "".join(chr(c) for c in ring)
# the ring wraps around: some rotation of it holds the message whole
check("blink.sh: the loop prints its message into the ring", any("loop: 1000" in text[i:] + text[:i] for i in range(16)), text)

# 3. variables, compares, built-in and inline messages
src = """setup:
    MSGB "READY"
    MSG "HI"
    CLR v0
    INC v0
    INC v0
    INC v0
    DEC v0
    LDA v0
    IFEQ 2
    HI gpio0
    LDA v0
    IFGT 2
    HI gpio1
    LDA v0
    IFNE 5
    HI gpio5
    END
loop:
    JMPB loop
"""
prog = assemble(src); image(prog)
g, ring = sample([20000], ["gpio"])[0][0], sample([20000], ["0x%x" % (0x50 + i) for i in range(16)])[0]
check("v0 == 2: gpio0 high", g & 1 == 1, hex(g))
check("v0 > 2 is false: gpio1 stays low", g & 2 == 0, hex(g))
check("v0 != 5: gpio5 high", g & 0x20 == 0x20, hex(g))
check("MSGB and MSG fill the ring", "".join(chr(c) for c in ring).startswith("READYHI"), ring)
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
