#!/usr/bin/env python3
"""tzlow.py against a fake device that answers frames the way vm.asm does (no hardware, no gpsim).
usage: test_tzlow.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tzlow import Device, Err, run, compile_file

class FakeLink:
    """The device side of the link, as in vm.asm (linkframe / lread / lw_sys)."""
    def __init__(self):
        self.ee, self.ptr, self.ring, self.stopped, self.bits, self.frames = bytearray(256), 0, list(b"READY"), False, [0] * 7, 0
    def xfer(self, f):
        self.frames += 1
        cmd = f[0]
        if cmd & 0x80:
            n, v = cmd & 0x7F, f[1]
            if n == 0x20: self.ptr = v; return 0
            if n == 0x21: self.ee[self.ptr] = v; self.ptr = (self.ptr + 1) & 0xFF; return 0
            if n == 0x23 and v == 0: self.stopped = True; return 0
            if n == 0x23 and v == 1: self.stopped = False; return 0
            if n >= 7 or n == 4 or v > 3 or (n >= 5 and v > 1): return 0xFF
            self.bits[n] = v & 1 if v < 2 else 0; return 0
        n = cmd
        if n == 0x21: v = self.ee[self.ptr]; self.ptr = (self.ptr + 1) & 0xFF; return v
        if n == 0x22: return self.ring.pop(0) if self.ring else 0
        if n == 0x23: return 1 if self.stopped else 0
        if n >= 7: return 0xFF
        return self.bits[n]

fails = 0
def check(name, cond, got=None):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
    fails += not cond
def refused(line):
    try: run(dev, line, out=lambda s: None); return False
    except Err: return True

link = FakeLink(); dev = Device(link); said = []
out = said.append
run(dev, "ls", out)
check("ls lists the gpio, timer and out nodes", "/dev/gpio/0" in said[0] and "/dev/timer/0/repeat" in said[0] and "/dev/out" in said[0], said)
run(dev, "echo 1 > /dev/gpio/4", out)
check("echo 1 > /dev/gpio/4 sets node 2", link.bits[2] == 1, link.bits)
said.clear(); run(dev, "cat /dev/gpio/4", out)
check("cat /dev/gpio/4 reads 1", said == ["1"], said)
check("echo 1 > /dev/gpio/3 is refused (input only)", refused("echo 1 > /dev/gpio/3"))
check("echo in > /dev/timer/0/finish is refused", refused("echo in > /dev/timer/0/finish"))
check("cat /dev/gpio/2 is refused (the link)", refused("cat /dev/gpio/2"))
check("an unknown command is refused", refused("rm /dev/gpio/0"))
said.clear(); run(dev, "cat /dev/out", out)
check("cat /dev/out drains the ring", said == ["READY"] and not link.ring, said)
blink = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vm", "examples", "blink.sh")
prog = compile_file(blink)
said.clear(); run(dev, "load " + blink, out)
check("load writes the program to the EEPROM", bytes(link.ee[:len(prog)]) == bytes(prog), link.ee[:len(prog)])
check("load restarts the machine", not link.stopped)
check("load reports the size", said == ["%d bytes of 255 loaded" % len(prog)], said)
run(dev, "stop", out)
check("stop", link.stopped)
said.clear(); run(dev, "dump 4", out)
check("dump shows the header bytes", said[0] == " ".join("%02X" % b for b in prog[:4]), said)
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
