#!/usr/bin/env python3
"""Tests for the reference virtual machine.  usage: test_vm.py"""
import sys
from nodevm import assemble, disassemble, VM, Fault

fails = 0
def check(name, cond, got=None):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
    fails += not cond

def run(src, ms, setup=None):
    vm = VM(assemble(src))
    if setup: setup(vm)
    vm.run(ms * 1000)
    return vm

# ---- blink: gpio4 flips every 500 ms
blink = open("examples/blink.s").read()
vm = run(blink, 5200)
flips = [(t, v) for t, n, v in vm.trace if n == "gpio4"]
check("blink: 10 flips in 5.2 s", len(flips) == 10, len(flips))
check("blink: it alternates 1,0,1,0...", [v for _, v in flips] == [1, 0] * 5, flips)
gaps = [b[0] - a[0] for a, b in zip(flips, flips[1:])]
check("blink: every 500 ms (+-1 ms)", all(abs(g - 500000) < 1000 for g in gaps), gaps)
check("blink: the program is 17 bytes", len(assemble(blink)) == 17, len(assemble(blink)))
dis = disassemble(assemble(blink))
check("disassembler shows the sections", all(x in dis for x in ("setup:", "intr:", "loop:", "IFH gpio4")), dis)

# ---- a condition runs the next instruction when true and skips it when false
t = "setup:\n IFH gpio4\n HI gpio5\n HI gpio0\n END\nloop:\n JMPB loop\n"
vm = run(t, 5, lambda m: m.ext.update(gpio4=1))
check("IFH true: the next instruction runs", [(n, v) for _, n, v in vm.trace] == [("gpio5", 1), ("gpio0", 1)], vm.trace)
vm = run(t, 5, lambda m: m.ext.update(gpio4=0))
check("IFH false: the next instruction is skipped", [(n, v) for _, n, v in vm.trace] == [("gpio0", 1)], vm.trace)

# ---- skipping an instruction that is longer than one byte
t = 'setup:\n IFH gpio4\n MSG "HELLO"\n HI gpio0\n END\nloop:\n JMPB loop\n'
vm = run(t, 5, lambda m: m.ext.update(gpio4=0))
check("a skipped MSG is skipped whole", vm.out == [] and [(n, v) for _, n, v in vm.trace] == [("gpio0", 1)], (vm.out, vm.trace))
vm = run(t, 5, lambda m: m.ext.update(gpio4=1))
check("MSG copies the text to the output ring", "".join(map(chr, vm.out)) == "HELLO", vm.out)

# ---- the output ring keeps the newest 16 bytes
t = 'setup:\n MSG "0123456789ABCDEF"\n MSG "XYZ"\n END\nloop:\n JMPB loop\n'
vm = run(t, 5)
check("ring: the oldest bytes are dropped", "".join(map(chr, vm.out)) == "3456789ABCDEFXYZ"[-16:], vm.out)

# ---- compare and variables: count 5 intr events then raise gpio5
t = """setup:
    HI timer_repeat
    TLIM 10
    CLR v0
    END
intr:
    LO timer_finish
    INC v0
    RETI
loop:
    LDA v0
    IFEQ 5
    HI gpio5
    JMPB loop
"""
vm = run(t, 100)
check("var + compare: gpio5 goes high after 5 ticks", ("gpio5", 1) in [(n, v) for _, n, v in vm.trace], vm.trace)
first = [tm for tm, n, v in vm.trace if n == "gpio5"][0]
check("   ... at about 50 ms", 49000 < first < 52000, first)
check("   ... and v0 is 10 after 100 ms", vm.vars[0] in (9, 10), vm.vars)

# ---- IFGT with an adc reading, and its negation
t = """setup:
    LDA adc0
    IFGT 500
    HI gpio0
    IFLE 500
    HI gpio1
    END
loop:
    JMPB loop
"""
vm = run(t, 5, lambda m: m.ext.update(adc0=700))
check("adc 700 > 500: gpio0 high, gpio1 not", [(n, v) for _, n, v in vm.trace] == [("gpio0", 1)], vm.trace)
vm = run(t, 5, lambda m: m.ext.update(adc0=300))
check("adc 300 <= 500: gpio1 high, gpio0 not", [(n, v) for _, n, v in vm.trace] == [("gpio1", 1)], vm.trace)

# ---- an interrupt never comes between LDA and its compare (the interrupt's own LDA would change A)
t = """setup:
    HI timer_repeat
    TLIM 1
    END
intr:
    LO timer_finish
    LDA v1
    RETI
loop:
    LDA v0
    IFEQ 0
    INC v2
    INC v3
    JMPB loop
"""
def prime(m): m.vars[1] = 99          # the interrupt loads 99 into A; the loop expects 0
vm = VM(assemble(t)); prime(vm); vm.run(200000)
check("LDA/compare: the loop never saw the interrupt's A", vm.vars[2] == vm.vars[3] and vm.vars[3] > 1000, vm.vars)
vm = VM(assemble(t)); vm.protect_a = False; prime(vm); vm.run(200000)
check("   (without the rule the test does catch the clobbering)", vm.vars[2] != vm.vars[3], vm.vars)

# ---- the built-in messages: one byte each, kept in the firmware
t = 'setup:\n MSGB "READY"\n MSGB "OK"\n MSG "CUSTOM"\n END\nloop:\n JMPB loop\n'
prog = assemble(t)
vm = VM(prog); vm.run(5000)
check("MSGB: built-in messages go to the ring", "".join(map(chr, vm.out)) == "READYOKCUSTOM", vm.out)
check("MSGB costs one byte (READY=5 chars, OK=2)", len(assemble('setup:\n MSGB "READY"\n END\nloop:\n JMPB loop\n')) == len(assemble('setup:\n MSG "R"\n END\nloop:\n JMPB loop\n')) - 1)
check("MSGB shows in the disassembly", 'MSGB "READY"' in disassemble(prog), disassemble(prog))
try:
    assemble('setup:\n MSGB "NOPE"\n END\n'); ok = False
except ValueError:
    ok = True
check("assembler refuses a message that is not built in", ok)
vm = VM(prog, builtin=["OK"])
try:
    vm.run(5000); ok = False
except Fault:
    ok = True
check("a target with fewer built-in messages faults on a missing one", ok)

# ---- faults
for name, src in (("write to an input-only node", "setup:\n HI gpio3\n END\nloop:\n JMPB loop\n"),
                  ("END inside intr", "intr:\n END\nsetup:\n HI timer_repeat\n TLIM 1\n END\n")):
    try:
        run(src, 20); ok = False
    except Fault:
        ok = True
    check("fault: " + name, ok)

# ---- assembler limits
for name, src in (("MSG over 16 characters", 'setup:\n MSG "12345678901234567"\n END\n'),
                  ("JMPF over 15 bytes", "setup:\n JMPF far\n" + " NOP\n" * 20 + "far:\n END\n"),
                  ("a non-ASCII message", 'setup:\n MSG "あ"\n END\n')):
    try:
        assemble(src); ok = False
    except ValueError:
        ok = True
    check("assembler refuses " + name, ok)

# ---- the converter: tzsh -> bytecode, then run it
from tzsh2bc import compile_, Err
prog, _ = compile_(open("examples/blink.sh").read())
vm = VM(prog); vm.run(5200000)
fl = [(t, v) for t, n, v in vm.trace if n == "gpio4"]
check("tzsh2bc: blink.sh flips gpio4 every 500 ms", len(fl) >= 10 and all(abs((b[0] - a[0]) - 500000) < 1000 for a, b in zip(fl, fl[1:])), fl[:4])
check("tzsh2bc: the loop's message reaches the ring", "".join(map(chr, vm.out)).endswith("loop: 1000"), vm.out)
check("tzsh2bc: it is %d bytes (limit 255)" % len(prog), len(prog) <= 255)
for name, src in (("a 17 character message", 'loop { echo "12345678901234567"; }'),
                  ("a fifth variable", "setup { a = 0; b = 0; c = 0; d = 0; e = 0; }"),
                  ("while", "loop { while (1) { } }"),
                  ("a node this chip does not have", 'setup { echo 1 > /dev/gpio/13; }'),
                  ("n = n + 7", "loop { n = n + 7; }"),
                  ("a statement outside the blocks", "echo 1 > /dev/gpio/4;")):
    try:
        compile_(src); ok = False
    except (Err, ValueError):
        ok = True
    check("tzsh2bc refuses " + name, ok)

print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
