#!/usr/bin/env python3
"""tzsh (the subset the 4 bit VM can express) -> bytecode.   usage: tzsh2bc.py script.sh [-asm]

Blocks: setup { }  intr { }  loop { }.   Statements:
  echo 0|1|in|up > /dev/gpio/N          echo 0|1 > /dev/timer/0/repeat|finish     echo MS > /dev/timer/0/limit
  echo "text" (<= 16 ASCII chars)       NAME = 0     NAME = NAME + 1     NAME = NAME - 1      (4 variables)
  if (COND) { } [else { }]   with COND = /dev/gpio/N == "0"|"1", /dev/timer/0/finish == "1",
                             NAME or /dev/adc/N  with  == != < <= > >=  NUMBER
Everything else is an error, never silently dropped."""
import re, sys
from nodevm import assemble, NODES_12F683, BUILTIN_MSGS

class Err(Exception):
    pass

TOK = re.compile(r'\s*(?:(//[^\n]*|#[^\n]*)|("[^"]*")|(==|!=|<=|>=)|([{}();=<>+\-])|([^\s{}();=<>+\-"]+))')

def tokens(src):
    out, pos = [], 0
    src = src.rstrip()
    while pos < len(src):
        m = TOK.match(src, pos)
        if not m: raise Err("cannot read near %r" % src[pos:pos + 12])
        pos = m.end()
        if m.group(1) is not None: continue
        out.append(m.group(2) or m.group(3) or m.group(4) or m.group(5))
    return out

class C:
    def __init__(self, src, nodes):
        self.t, self.i, self.nodes = tokens(src), 0, {n for n, _ in nodes}
        self.vars, self.lines, self.n = {}, [], 0

    def peek(self): return self.t[self.i] if self.i < len(self.t) else None
    def take(self, want=None):
        x = self.peek()
        if x is None or (want is not None and x != want): raise Err("expected %s, found %s" % (want or "more", x))
        self.i += 1; return x
    def label(self): self.n += 1; return "L%d" % self.n
    def emit(self, s): self.lines.append("    " + s)
    def var(self, name):
        if name not in self.vars:
            if len(self.vars) == 4: raise Err("more than 4 variables (%s)" % name)
            self.vars[name] = len(self.vars)
        return "v%d" % self.vars[name]

    def node(self, path):
        m = re.fullmatch(r"/dev/gpio/(\d+)", path)
        if m: n = "gpio" + m.group(1)
        elif path == "/dev/timer/0/finish": n = "timer_finish"
        elif path == "/dev/timer/0/repeat": n = "timer_repeat"
        else: raise Err("no bit node for %s" % path)
        if n not in self.nodes: raise Err("this chip has no node %s" % path)
        return n

    def program(self):
        seen = set()
        while self.peek() is not None:
            name = self.take()
            if name not in ("setup", "intr", "loop") or name in seen:
                raise Err("expected setup / intr / loop (once each), found %s" % name)
            seen.add(name)
            self.lines.append(name + ":")
            self.block()
            if name == "setup": self.emit("END")
            elif name == "intr": self.emit("RETI")
            else: self.lines.append("    @LOOPEND")
        return self.lines

    def block(self):
        self.take("{")
        while self.peek() != "}":
            self.stmt()
        self.take("}")

    def stmt(self):
        w = self.take()
        if w == "echo": return self.echo()
        if w == "if": return self.if_()
        if w in ("while", "break", "exit"): raise Err("%s is not available in the VM subset" % w)
        # NAME = 0 | NAME = NAME +- 1
        self.take("=")
        v = self.var(w); a = self.take()
        if a == "0" and self.peek() == ";": self.emit("CLR " + v)
        else:
            if a != w: raise Err("only NAME = 0 and NAME = NAME +- 1 are available (%s)" % w)
            op = self.take(); k = self.take()
            if op not in ("+", "-") or k != "1": raise Err("only NAME = NAME +- 1 is available (%s)" % w)
            self.emit(("INC " if op == "+" else "DEC ") + v)
        self.take(";")

    def echo(self):
        a = self.take()
        if self.peek() == ">":
            self.take(); path = self.take(); self.take(";")
            if path.startswith("/dev/gpio/"):
                ins = {"0": "LO", "1": "HI", "in": "IN", "up": "UP"}.get(a)
            elif path in ("/dev/timer/0/repeat", "/dev/timer/0/finish"):
                ins = {"0": "LO", "1": "HI"}.get(a)
            elif path == "/dev/timer/0/limit":
                if not re.fullmatch(r"\d+", a): raise Err("limit takes a number of ms")
                self.emit("TLIM " + a); return
            else: raise Err("no node %s" % path)
            if ins is None: raise Err("bad value %s for %s" % (a, path))
            self.emit("%s %s" % (ins, self.node(path)))
            return
        self.take(";")
        s = a[1:-1] if a.startswith('"') else a
        self.emit('MSGB "%s"' % s if s in BUILTIN_MSGS else 'MSG "%s"' % s)

    def if_(self):
        self.take("(")
        a = self.take(); op = self.take(); b = self.take(); self.take(")")
        els, end = self.label(), self.label()
        if a.startswith("/dev/gpio") or a == "/dev/timer/0/finish":
            if op != "==" or b.strip('"') not in ("0", "1"): raise Err('a pin is compared as == "0" or == "1"')
            self.emit(("IFH " if b.strip('"') == "0" else "IFL ") + self.node(a))      # the opposite jumps to else
        else:
            if re.fullmatch(r"/dev/adc/[0-3]", a): self.emit("LDA adc" + a[-1])
            else: self.emit("LDA " + self.var(a))
            neg = {"==": "IFNE", "!=": "IFEQ", "<": "IFGE", "<=": "IFGT", ">": "IFLE", ">=": "IFLT"}
            if op not in neg or not re.fullmatch(r"\d+", b.strip('"')): raise Err("compare with == != < <= > >= and a number")
            self.emit("%s %s" % (neg[op], b.strip('"')))
        self.emit("JMPF " + els)
        self.block()
        if self.peek() == "else":
            self.take(); self.emit("JMPF " + end); self.lines.append(els + ":")
            self.block(); self.lines.append(end + ":")
        else:
            self.lines.append(els + ":")

def compile_(src, nodes=NODES_12F683):
    """returns (bytes, asm text)"""
    lines = C(src, nodes).program()
    if "    @LOOPEND" not in lines:
        text = "\n".join(lines) + "\n"
        return assemble(text, nodes), text
    last = None
    for kind in ("JMPB", "JMPL"):                  # a long loop needs NOPs so that the jump is a 16 byte step
        for pad in range(16):
            text = "\n".join(l if l != "    @LOOPEND" else "    NOP\n" * pad + "    %s loop" % kind for l in lines) + "\n"
            try:
                return assemble(text, nodes), text
            except ValueError as e:
                last = e
    raise Err("the program does not fit: %s" % last)

if __name__ == "__main__":
    from nodevm import disassemble
    try:
        prog, text = compile_(open(sys.argv[1]).read())
    except (Err, ValueError) as e:
        print("tzsh2bc: %s" % e); sys.exit(1)
    if "-asm" in sys.argv: print(text)
    print(" ".join("%02X" % b for b in prog)); print("%d bytes" % len(prog)); print(disassemble(prog))
