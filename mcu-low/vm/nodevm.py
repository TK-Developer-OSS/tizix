#!/usr/bin/env python3
"""The mcu-low "4 bit virtual machine": reference implementation, assembler and disassembler.

Every instruction is one byte: the upper 4 bits are the mnemonic, the lower 4 bits a value.
A condition says: if true, run the NEXT instruction, if false, skip it (and go on after it).
The next instruction is normally a jump, which makes "if" a fixed two byte pattern.
The result lives in the program counter, so an interrupt may come between the two. It may not come
between LDA and the compare after it (the interrupt's own LDA would change A).

  0 SYS  0 END  1 NOP  2 RETI  3 MSG (ASCII follows, the last character has bit 7 set; <= 16 chars)
         4-15 MSGB: a built-in message kept in the firmware (flash), 1 byte in the script (option)
  1 LO n   2 HI n   3 IN n   4 UP n          n = bit node number (a pin, or timer finish / repeat)
  5 IFL n  6 IFH n                           run the next instruction if the node is low / high
  7 TLIM k                                   timer limit (ms) = constant k; constant 0 is 0 = stop
  8 LDA s                                    A = adc channel s (0-3), or variable s-4 (4-7)
  9 IFGT k  A IFLT k  B IFEQ k               bit 3 negates (IFLE, IFGE, IFNE); k = constant 0-7
  C VAR v,op                                 v = bits 3-2 (variable 0-3), op = bits 1-0: 0 inc 1 dec 2 clr
  D JMPF n  forward n bytes (0-15)   E JMPB n  back n bytes (1-15)   F JMPL n  n<8: forward 16*n, else back 16*(n-7)

Program: [setup][intr][loop] (absolute offsets, 0xFF = none) [nconst] [nconst * 16 bit little endian]
(constants 1..nconst; constant 0 is always 0) then the code.  Offsets are from the start of the program.
"""
import re, sys

class Fault(Exception):
    pass

# ---- the node table: which bit nodes this chip has (index = the 4 bit operand)
PIN, PIN_IN, FINISH, REPEAT = "pin", "pin_in", "finish", "repeat"
NODES_12F683 = [("gpio0", PIN), ("gpio1", PIN), ("gpio4", PIN), ("gpio5", PIN), ("gpio3", PIN_IN),
                ("timer_finish", FINISH), ("timer_repeat", REPEAT)]

MN = {0: "SYS", 1: "LO", 2: "HI", 3: "IN", 4: "UP", 5: "IFL", 6: "IFH", 7: "TLIM", 8: "LDA",
      9: "IFGT", 10: "IFLT", 11: "IFEQ", 12: "VAR", 13: "JMPF", 14: "JMPB", 15: "JMPL"}
SYSN = {0: "END", 1: "NOP", 2: "RETI", 3: "MSG"}
# the built-in messages (SYS 4-15): they live in the firmware, so a script spends one byte on them.
# A target may carry a different list, or none (a chip short of flash).
BUILTIN_MSGS = ["OK", "ERROR", "READY", "START", "STOP", "DONE", "TIMER", "ON", "OFF", "HIGH", "LOW", "ALARM"]
VARN = {0: "INC", 1: "DEC", 2: "CLR"}
NEG = {9: "IFLE", 10: "IFGE", 11: "IFNE"}

def jump_delta(op, n):
    """(signed byte distance from the end of the jump) for D E F"""
    if op == 13: return n
    if op == 14: return -n
    return 16 * n if n < 8 else -16 * (n - 7)

# ---------------------------------------------------------------- the machine
class VM:
    US_PER_INSTR = 30                   # a guess for a real chip; only the timers care

    def __init__(self, prog, nodes=NODES_12F683, builtin=BUILTIN_MSGS):
        self.nodes = nodes
        self.builtin = builtin
        self.setup_off, self.intr_off, self.loop_off, nconst = prog[0], prog[1], prog[2], prog[3]
        self.consts = [0] + [prog[4 + 2 * i] | prog[5 + 2 * i] << 8 for i in range(nconst)]
        self.mem = bytes(prog)
        self.A = 0
        self.vars = [0] * 4
        self.t = 0                      # virtual time, microseconds
        self.trace = []                 # (time_us, what, value)
        self.out = []                   # the output ring (<= 16 bytes, the oldest is dropped)
        self.level = {n: 0 for n, k in nodes if k in (PIN,)}
        self.mode = {n: "in" for n, k in nodes if k in (PIN, PIN_IN)}
        self.ext = {}                   # what the outside world drives onto input pins
        self.limit_us = 0; self.next_exp = None; self.finish = 0; self.repeat = 0
        self.pending = 0; self.in_intr = False; self.ret = 0
        self.no_dispatch = False; self.halted = False
        self.protect_a = True           # no interrupt right after LDA (the alternative is saving A)
        self.pc = self.setup_off if self.setup_off != 0xFF else self.loop_off
        if self.pc == 0xFF: self.halted = True
        self.ncode = 4 + 2 * nconst
        if self.setup_off == 0xFF and self.loop_off == 0xFF and self.intr_off == 0xFF: self.halted = True
        self.idle = False

    # ---- helpers
    def node(self, n):
        if n >= len(self.nodes): raise Fault("no such node %d" % n)
        return self.nodes[n]

    def read_bit(self, n):
        name, kind = self.node(n)
        if kind == FINISH: return self.finish
        if kind == REPEAT: return self.repeat
        if self.mode[name] == "out": return self.level[name]
        if name in self.ext: return self.ext[name]
        return 1 if self.mode[name] == "up" else 0

    def write_bit(self, n, v):
        name, kind = self.node(n)
        if kind == FINISH: self.finish = v; return
        if kind == REPEAT: self.repeat = v; return
        if kind != PIN: raise Fault("node %s cannot be written" % name)
        self.level[name] = v; self.mode[name] = "out"
        self.trace.append((self.t, name, v))

    def set_mode(self, n, m):
        name, kind = self.node(n)
        if kind not in (PIN, PIN_IN): raise Fault("node %s has no direction" % name)
        if kind == PIN_IN and m == "out": raise Fault("node %s is input only" % name)
        self.mode[name] = m
        self.trace.append((self.t, name, m))

    def length(self, pc):
        """bytes of the instruction at pc"""
        b = self.mem[pc]
        if b == 0x03:
            n = 1
            while not self.mem[pc + n] & 0x80: n += 1
            return n + 1
        return 1

    def timers(self):
        while self.next_exp is not None and self.t >= self.next_exp:
            self.finish = 1; self.pending = 1
            if self.repeat: self.next_exp += self.limit_us
            else: self.next_exp = None; self.limit_us = 0

    # ---- one step; returns False when nothing more can happen without time passing
    def step(self):
        if self.halted: return False
        self.timers()
        if self.pending and self.intr_off != 0xFF and not self.in_intr and not self.no_dispatch and not self.idle:
            self.pending = 0; self.in_intr = True; self.ret = self.pc; self.pc = self.intr_off
        if self.idle:                                    # nothing to run: wait for the timer
            if self.next_exp is None: self.halted = True; return False
            self.t = max(self.t, self.next_exp); self.timers()
            if self.pending and self.intr_off != 0xFF:
                self.pending = 0; self.in_intr = True; self.ret = self.pc; self.idle = False; self.pc = self.intr_off
            return True
        self.no_dispatch = False
        if self.pc >= len(self.mem): raise Fault("ran off the end of the program")
        b = self.mem[self.pc]; op, v = b >> 4, b & 15
        n_next = self.pc + 1
        self.t += self.US_PER_INSTR
        def cond(c):
            nonlocal n_next
            if not c: n_next += self.length(n_next)      # the result is already in the program counter
        if op == 0:
            if v == 0:                                   # END
                if self.in_intr: raise Fault("END inside intr (use RETI)")
                if self.loop_off != 0xFF: n_next = self.loop_off
                elif self.intr_off != 0xFF: self.idle = True; n_next = self.pc
                else: self.halted = True
            elif v == 1: pass
            elif v == 2:
                if not self.in_intr: raise Fault("RETI outside intr")
                self.in_intr = False; n_next = self.ret
            elif v == 3:
                ln = self.length(self.pc)
                for c in self.mem[self.pc + 1:self.pc + ln]:
                    self.out.append(c & 0x7F)
                    if len(self.out) > 16: self.out.pop(0)
                n_next = self.pc + ln
            elif 4 <= v < 4 + len(self.builtin):
                for c in self.builtin[v - 4]:
                    self.out.append(ord(c))
                    if len(self.out) > 16: self.out.pop(0)
            else: raise Fault("bad SYS %d at %d" % (v, self.pc))
        elif op == 1: self.write_bit(v, 0)
        elif op == 2: self.write_bit(v, 1)
        elif op == 3: self.set_mode(v, "in")
        elif op == 4: self.set_mode(v, "up")
        elif op == 5: cond(self.read_bit(v) == 0)
        elif op == 6: cond(self.read_bit(v) == 1)
        elif op == 7:
            ms = self.consts[v]
            self.limit_us = ms * 1000
            self.next_exp = (self.t + self.limit_us) if ms else None
        elif op == 8:
            if v < 4: self.A = self.ext.get("adc%d" % v, 0)
            elif v < 8: self.A = self.vars[v - 4]
            else: raise Fault("bad LDA %d" % v)
            self.no_dispatch = self.protect_a                # an interrupt must not come between LDA and its compare
        elif op in (9, 10, 11):
            k = self.consts[v & 7] if (v & 7) < len(self.consts) else self.consts[0]
            if (v & 7) >= len(self.consts): raise Fault("no constant %d" % (v & 7))
            r = {9: self.A > k, 10: self.A < k, 11: self.A == k}[op]
            cond(r != bool(v & 8))
        elif op == 12:
            i, o = v >> 2, v & 3
            if o == 0: self.vars[i] = (self.vars[i] + 1) & 0xFFFF
            elif o == 1: self.vars[i] = (self.vars[i] - 1) & 0xFFFF
            elif o == 2: self.vars[i] = 0
            else: raise Fault("bad VAR op")
        else:
            if op == 14 and v == 0: raise Fault("JMPB 0")
            n_next = n_next + jump_delta(op, v)
        self.pc = n_next
        return True

    def run(self, us):
        """run for `us` microseconds of virtual time"""
        end = self.t + us
        while self.t < end:
            if self.idle and self.next_exp is not None and self.next_exp > end: self.t = end; break
            if not self.step(): self.t = end; break

# ---------------------------------------------------------------- the assembler
def assemble(text, nodes=NODES_12F683, builtin=BUILTIN_MSGS):
    names = {n: i for i, (n, k) in enumerate(nodes)}
    consts, items, labels, sections = [], [], {}, {}
    def const(v):
        v = int(v, 0)
        if v == 0: return 0
        if v not in consts:
            consts.append(v)
            if len(consts) > 15: raise ValueError("more than 15 constants")
        return consts.index(v) + 1
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.split(";")[0].strip()
        if not line: continue
        m = re.match(r"^(\w+):$", line)
        if m:
            items.append(("label", m.group(1))); continue
        parts = line.split(None, 1)
        mn, arg = parts[0].upper(), (parts[1].strip() if len(parts) > 1 else "")
        items.append(("ins", mn, arg, ln))
    code, pos, fix = [], 0, []
    def size(it):
        if it[1] == "MSG":
            s = re.match(r'^"(.*)"$', it[2]).group(1)
            if not 1 <= len(s) <= 16 or not s.isascii(): raise ValueError("MSG needs 1-16 ASCII characters")
            return 1 + len(s)
        return 1
    # first pass: addresses (relative to the start of the code)
    addr, lab = 0, {}
    for it in items:
        if it[0] == "label": lab[it[1]] = addr
        else: addr += size(it)
    # second pass: bytes
    out = []
    for it in items:
        if it[0] == "label": continue
        _, mn, arg, ln = it
        at = len(out)
        def node(a):
            if a not in names: raise ValueError("line %d: no node %s" % (ln, a))
            return names[a]
        def cn(a):
            k = const(a)
            if k > 7: raise ValueError("line %d: constant %s is beyond index 7 for a compare" % (ln, a))
            return k
        if mn == "END": out.append(0x00)
        elif mn == "NOP": out.append(0x01)
        elif mn == "RETI": out.append(0x02)
        elif mn == "MSGB":
            a = arg.strip('"')
            if a not in builtin: raise ValueError("line %d: no built-in message %s (have %s)" % (ln, a, " ".join(builtin)))
            out.append(0x04 + builtin.index(a))
        elif mn == "MSG":
            s = re.match(r'^"(.*)"$', arg).group(1)
            out.append(0x03); out += [ord(c) for c in s]; out[-1] |= 0x80
        elif mn in ("LO", "HI", "IN", "UP", "IFL", "IFH"):
            out.append({"LO": 0x10, "HI": 0x20, "IN": 0x30, "UP": 0x40, "IFL": 0x50, "IFH": 0x60}[mn] | node(arg))
        elif mn == "TLIM": out.append(0x70 | const(arg))
        elif mn == "LDA":
            m = re.match(r"^(adc|v)([0-3])$", arg)
            if not m: raise ValueError("line %d: LDA adc0-3 or v0-3" % ln)
            out.append(0x80 | int(m.group(2)) | (4 if m.group(1) == "v" else 0))
        elif mn in ("IFGT", "IFLT", "IFEQ", "IFLE", "IFGE", "IFNE"):
            base = {"IFGT": 9, "IFLE": 9, "IFLT": 10, "IFGE": 10, "IFEQ": 11, "IFNE": 11}[mn]
            neg = 8 if mn in ("IFLE", "IFGE", "IFNE") else 0
            out.append(base << 4 | neg | cn(arg))
        elif mn in ("INC", "DEC", "CLR"):
            m = re.match(r"^v([0-3])$", arg)
            if not m: raise ValueError("line %d: %s v0-3" % (ln, mn))
            out.append(0xC0 | int(m.group(1)) << 2 | {"INC": 0, "DEC": 1, "CLR": 2}[mn])
        elif mn in ("JMPF", "JMPB", "JMPL"):
            if arg not in lab: raise ValueError("line %d: no label %s" % (ln, arg))
            d = lab[arg] - (at + 1)
            if mn == "JMPF":
                if not 0 <= d <= 15: raise ValueError("line %d: JMPF distance %d is not 0-15" % (ln, d))
                out.append(0xD0 | d)
            elif mn == "JMPB":
                if not 1 <= -d <= 15: raise ValueError("line %d: JMPB distance %d is not 1-15" % (ln, d))
                out.append(0xE0 | -d)
            else:
                if d % 16 or not -128 <= d <= 112 or d == 0 and False: raise ValueError("line %d: JMPL needs a 16 byte step (%d)" % (ln, d))
                out.append(0xF0 | (d // 16 if d >= 0 else 7 + (-d) // 16))
        else: raise ValueError("line %d: unknown %s" % (ln, mn))
    base = 4 + 2 * len(consts)
    def sec(n): return base + lab[n] if n in lab else 0xFF
    head = bytes([sec("setup"), sec("intr"), sec("loop"), len(consts)])
    for c in consts: head += bytes([c & 255, c >> 8])
    prog = head + bytes(out)
    if len(prog) > 255: raise ValueError("the program is %d bytes (limit 255)" % len(prog))
    return prog

# ---------------------------------------------------------------- the disassembler
def disassemble(prog, nodes=NODES_12F683, builtin=BUILTIN_MSGS):
    nconst = prog[3]
    consts = [0] + [prog[4 + 2 * i] | prog[5 + 2 * i] << 8 for i in range(nconst)]
    base = 4 + 2 * nconst
    lines = ["setup=%s intr=%s loop=%s  constants %s" % tuple(
        ["-" if x == 0xFF else str(x) for x in prog[:3]] + [consts[1:]])]
    marks = {prog[0]: "setup:", prog[1]: "intr:", prog[2]: "loop:"}
    pc = base
    def nname(n): return nodes[n][0] if n < len(nodes) else "node%d?" % n
    while pc < len(prog):
        b = prog[pc]; op, v = b >> 4, b & 15; ln = 1
        if op == 0:
            if v == 3:
                ln = 1
                while not prog[pc + ln] & 0x80: ln += 1
                ln += 1
                t = 'MSG "%s"' % "".join(chr(c & 0x7F) for c in prog[pc + 1:pc + ln])
            elif 4 <= v < 4 + len(builtin): t = 'MSGB "%s"' % builtin[v - 4]
            else: t = SYSN.get(v, "SYS?%d" % v)
        elif op in (1, 2, 3, 4, 5, 6): t = "%s %s" % (MN[op], nname(v))
        elif op == 7: t = "TLIM %d" % consts[v] if v < len(consts) else "TLIM const%d?" % v
        elif op == 8: t = "LDA %s" % (("adc%d" % v) if v < 4 else ("v%d" % (v - 4)) if v < 8 else "?%d" % v)
        elif op in (9, 10, 11):
            k = consts[v & 7] if (v & 7) < len(consts) else "const%d?" % (v & 7)
            t = "%s %s" % (NEG[op] if v & 8 else MN[op], k)
        elif op == 12: t = "%s v%d" % (VARN.get(v & 3, "VAR?"), v >> 2)
        else:
            d = jump_delta(op, v); t = "%s -> %d" % (MN[op], pc + 1 + d - base)
        hexs = " ".join("%02X" % x for x in prog[pc:pc + ln])
        rel = pc - base
        lines.append("%s%3d  %-12s %s" % (marks.get(pc, "").ljust(7), rel, hexs, t))
        pc += ln
    return "\n".join(lines)

if __name__ == "__main__":
    prog = assemble(open(sys.argv[1]).read())
    print(" ".join("%02X" % b for b in prog)); print("%d bytes" % len(prog)); print(disassemble(prog))
