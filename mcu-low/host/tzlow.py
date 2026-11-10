#!/usr/bin/env python3
"""tzlow: the host command for mcu-low devices.  Talks the one wire link (500 baud 8N1, half duplex, the host is
master, one reply byte per frame) and lets the device be used the tizix way.

usage: tzlow.py [-p PORT] [--no-echo] [--sim] [COMMAND]      no COMMAND: an interactive prompt
  --sim: no hardware, a pretend device on this PC (the reference machine, blink.s in its EEPROM)
  ls                          the device's nodes
  cat PATH                    read a node (/dev/gpio/N, /dev/timer/0/finish|repeat, /dev/out = the message ring)
  echo V > PATH               write a node (gpio: 0 1 in up; timer flags: 0 1)
  load FILE                   compile (.sh: tzsh, .s: assembler), write it to the EEPROM, check it, restart
  dump [N]                    the first N bytes of the EEPROM (default 32), disassembled when they parse
  stop | start                stop the machine / restart it from the EEPROM
Wiring: the adapter's RX on the line, its TX through 1k-4.7k onto the line; the host then hears its own bytes
back (the echo, skipped).  With a separate driver that does not echo, pass --no-echo.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vm"))
from nodevm import assemble, disassemble, NODES_12F683

# the device's nodes: path -> link node number (the same numbers as the machine's bit nodes)
PATHS = {"/dev/gpio/0": 0, "/dev/gpio/1": 1, "/dev/gpio/4": 2, "/dev/gpio/5": 3, "/dev/gpio/3": 4,
         "/dev/timer/0/finish": 5, "/dev/timer/0/repeat": 6}
READONLY = {4}
EEPTR, EEDATA, RING, CTL = 0x20, 0x21, 0x22, 0x23
VALUES = {"0": 0, "1": 1, "in": 2, "up": 3}

class Err(Exception): pass

class SerialLink:
    """One frame out, one byte back, over pyserial."""
    def __init__(self, port, echo=True):
        import serial
        self.s = serial.Serial(port, 500, timeout=0.2)
        self.echo = echo
        time.sleep(0.1)
        self.s.reset_input_buffer()
    def xfer(self, frame):
        self.s.write(bytes(frame))
        if self.echo and self.s.read(len(frame)) != bytes(frame):
            raise Err("the line does not echo what was sent (check the wiring, or --no-echo)")
        r = self.s.read(1)
        if not r: raise Err("no reply from the device")
        return r[0]

class SimLink:
    """No hardware: a pretend device on this PC.  The reference machine (nodevm.py) runs the program in a pretend
    EEPROM, kept up with the wall clock, and the frames are answered the way vm.asm answers them."""
    def __init__(self, prog):
        self.ee = bytearray(256); self.ee[:len(prog)] = prog
        self.ptr, self.stopped = 0, False
        self.restart()
    def restart(self):
        from nodevm import VM
        self.vm, self.stopped, self.last = VM(bytes(self.ee)), False, time.time()
    def catch_up(self):
        from nodevm import Fault
        now = time.time()
        if not self.stopped:
            try: self.vm.run(int((now - self.last) * 1e6))
            except Fault as e: print("tzlow: (sim) the machine stopped: %s" % e, file=sys.stderr); self.stopped = True
        self.last = now
    def xfer(self, f):
        from nodevm import Fault
        self.catch_up()
        vm, cmd = self.vm, f[0]
        try:
            if cmd & 0x80:
                n, v = cmd & 0x7F, f[1]
                if n == EEPTR: self.ptr = v; return 0
                if n == EEDATA: self.ee[self.ptr] = v; self.ptr = (self.ptr + 1) & 0xFF; return 0
                if n == CTL and v == 0: self.stopped = True; return 0
                if n == CTL and v == 1: self.restart(); return 0
                if n >= 7 or n in READONLY or v > 3 or (n >= 5 and v > 1): return 0xFF
                if v < 2: vm.write_bit(n, v)
                else: vm.set_mode(n, "in" if v == 2 else "up")
                return 0
            if cmd == EEDATA: v = self.ee[self.ptr]; self.ptr = (self.ptr + 1) & 0xFF; return v
            if cmd == RING: return vm.out.pop(0) if vm.out else 0
            if cmd == CTL: return 1 if self.stopped else 0
            if cmd >= 7: return 0xFF
            return vm.read_bit(cmd)
        except Fault:
            return 0xFF

class Device:
    def __init__(self, link): self.link = link
    def write(self, node, v): return self.link.xfer([0x80 | node, v])
    def read(self, node): return self.link.xfer([node])
    def ok(self, r, what):
        if r != 0: raise Err("%s: refused" % what)

    def ls(self):
        return sorted(PATHS) + ["/dev/out"]

    def cat(self, path):
        if path == "/dev/out":
            out = []
            while len(out) < 16:            # the ring holds at most 16 bytes
                b = self.read(RING)
                if b == 0: break
                out.append(b)
            return bytes(out).decode("ascii", "replace")
        n = self.node(path)
        r = self.read(n)
        if r == 0xFF: raise Err("%s: refused" % path)
        return str(r)

    def echo(self, value, path):
        n = self.node(path)
        if n in READONLY: raise Err("%s: read only" % path)
        if value not in VALUES or (n >= 5 and VALUES[value] > 1): raise Err("%s: bad value %s" % (path, value))
        self.ok(self.write(n, VALUES[value]), path)

    def node(self, path):
        if path not in PATHS: raise Err("%s: no such node" % path)
        return PATHS[path]

    def stop(self): self.ok(self.write(CTL, 0), "stop")
    def start(self): self.ok(self.write(CTL, 1), "start")

    def eeprom(self, n):
        self.ok(self.write(EEPTR, 0), "eeprom pointer")
        return bytes(self.read(EEDATA) for _ in range(n))

    def load(self, prog):
        if len(prog) > 255: raise Err("the program is %d bytes (255 at most)" % len(prog))
        self.stop()
        self.ok(self.write(EEPTR, 0), "eeprom pointer")
        for b in prog: self.ok(self.write(EEDATA, b), "eeprom write")
        if self.eeprom(len(prog)) != bytes(prog): raise Err("the EEPROM reads back different")
        self.start()

def compile_file(path):
    top = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    if not os.path.exists(path) and os.path.exists(os.path.join(top, path)):
        path = os.path.join(top, path)      # make run works in mcu-low/<device>: also look from the top
    src = open(path).read()
    if path.endswith(".sh"):
        from tzsh2bc import compile_
        return compile_(src)[0]
    return assemble(src)

def run(dev, line, out=print):
    w = line.split()
    if not w: return
    c = w[0]
    if c == "ls": out("\n".join(dev.ls()))
    elif c == "cat" and len(w) == 2: out(dev.cat(w[1]))
    elif c == "echo" and len(w) == 4 and w[2] == ">": dev.echo(w[1], w[3])
    elif c == "load" and len(w) == 2:
        prog = compile_file(w[1]); dev.load(prog); out("%d bytes of 255 loaded" % len(prog))
    elif c == "dump" and len(w) <= 2:
        data = dev.eeprom(int(w[1]) if len(w) == 2 else 32)
        out(" ".join("%02X" % b for b in data))
        try: out(disassemble(data))
        except Exception: pass
    elif c == "stop" and len(w) == 1: dev.stop()
    elif c == "start" and len(w) == 1: dev.start()
    else: raise Err("%s: not understood (see tzlow.py -h)" % line)

def main(argv):
    port, echo, args = "/dev/ttyUSB0", True, list(argv)
    if args and args[0] in ("-h", "--help"): print(__doc__); return 0
    if len(args) >= 2 and args[0] == "-p": port = args[1]; args = args[2:]
    if args and args[0] == "--no-echo": echo = False; args = args[1:]
    if args and args[0] == "--sim":
        here = os.path.dirname(os.path.abspath(__file__))
        dev = Device(SimLink(compile_file(os.path.join(here, "..", "vm", "examples", "blink.s"))))
        args = args[1:]
        if not args: print("tzlow: a pretend device on this PC (blink.s in its EEPROM: gpio/4 flips every 500 ms)")
    else:
        dev = Device(SerialLink(port, echo))
    lines = [" ".join(args)] if args else None
    status = 0
    while True:
        if lines is None:
            try: line = input("[pic]# ")
            except EOFError: print(); return 0
            if line.strip() in ("exit", "quit"): return 0
        elif lines: line = lines.pop()
        else: return status
        try: run(dev, line)
        except (Err, OSError, ValueError) as e: print("tzlow: %s" % e, file=sys.stderr); status = 1

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
