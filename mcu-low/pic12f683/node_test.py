#!/usr/bin/env python3
"""Run node.asm in gpsim: send commands on the one wire (GP2) at 500 baud, decode the single reply byte
of each, and check the pin state registers after each command.   usage: node_test.py"""
import re, subprocess, sys, os

B, START, GAP, BYTEGAP = 2000, 20000, 90000, 3000
# (name, bytes to send, expected reply, check after: (register, mask, expected) or None)
STEPS = [
    ("write gpio0 = 1",       [0x80, 1], 0x00, [("gpio", 0x01, 0x01), ("trisio", 0x01, 0x00)]),
    ("write gpio0 = 0",       [0x80, 0], 0x00, [("gpio", 0x01, 0x00), ("trisio", 0x01, 0x00)]),
    ("write gpio4 = 1",       [0x84, 1], 0x00, [("gpio", 0x10, 0x10), ("trisio", 0x10, 0x00)]),
    ("read gpio4 (an output)",[0x04],    0x01, None),
    ("read gpio0 (an output)",[0x00],    0x00, None),
    ("write gpio1 = up",      [0x81, 3], 0x00, [("trisio", 0x02, 0x02), ("wpu", 0x02, 0x02)]),
    ("write gpio1 = in",      [0x81, 2], 0x00, [("trisio", 0x02, 0x02), ("wpu", 0x02, 0x00)]),
    ("write gpio5 = 1", [0x85, 1], 0x00, [("trisio", 0x20, 0x00)]),
    ("write gpio5 = in",      [0x85, 2], 0x00, [("trisio", 0x20, 0x20)]),
    ("write gpio2 (the line)",[0x82, 1], 0xFF, [("trisio", 0x04, 0x04)]),
    ("write gpio3 (input only)", [0x83, 1], 0xFF, None),
    ("read gpio3",            [0x03],    None, None),     # any 00 or 01: the pin is not driven in the model
    ("read gpio2 (the line)", [0x02],    0xFF, None),
    ("read node 6",           [0x06],    0xFF, None),
    ("bad value",             [0x80, 7], 0xFF, None),
    ("a node that is not there", [0xFF, 1], 0xFF, None),
]

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    subprocess.run(["gpasm", "-p", "p12f683", "-o", "node.hex", "node.asm"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pairs, last, checks = [], 1, []
    for n, (_, send, _, chk) in enumerate(STEPS):
        t = START + n * GAP
        for byte in send:
            for b in [0] + [(byte >> i) & 1 for i in range(8)] + [1]:
                if b != last: pairs.append((t, b)); last = b
                t += B
            t += BYTEGAP
        checks.append(START + n * GAP + GAP - 5000)
    end = START + GAP * len(STEPS) + 1000
    stc = "load s node.cod\nstimulus asynchronous_stimulus\ninitial_state 1\nstart_cycle 0\ndigital\n{ %s }\nname rxs\nend\n" % \
          ", ".join("%d,%d" % p for p in pairs)
    stc += "node n_line\nattach n_line rxs p12f683.gpio2\nlog on node.log\nlog w gpio\nlog w trisio\n"
    for n, c in enumerate(checks):
        stc += "break c %d\nrun\nx gpio\nx trisio\nx wpu\n" % c
    stc += "quit\n"
    open("node_run.stc", "w").write(stc)
    if os.path.exists("node.log"): os.remove("node.log")
    out = subprocess.run(["gpsim", "-i", "-c", "node_run.stc"], check=True, timeout=600,
                         capture_output=True, text=True).stdout
    regs = []
    cur = {}
    for m in re.finditer(r"(gpio|trisio|wpu)\[0x[0-9a-f]+\] = 0x([0-9a-f]+)", out):
        cur[m.group(1)] = int(m.group(2), 16)
        if m.group(1) == "wpu": regs.append(dict(cur))

    log = open("node.log").read()
    ev, level = [(0, 1)], 1
    for m in re.finditer(r"^0x([0-9A-F]+) p12f683 \S+ \S+ (bsf|bcf)\s+gpio,2", log, re.M):
        lv = 1 if m.group(2) == "bsf" else 0
        if lv != level: ev.append((int(m.group(1), 16), lv)); level = lv
    def at(t):
        v = 1
        for c, l in ev:
            if c <= t: v = l
        return v
    replies, seen = [], []
    for c, l in ev:
        if l == 0 and at(c - 1) == 1 and (not seen or c - seen[-1] > 30000): seen.append(c)
    for c in seen:
        replies.append(sum(at(c + B // 2 + B * (k + 1)) << k for k in range(8)))
    fails = 0
    def check(name, cond, got=None):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
        fails += not cond
    check("one reply per command", len(replies) == len(STEPS), (len(replies), replies))
    for i, (name, _, want, chk) in enumerate(STEPS):
        r = replies[i] if i < len(replies) else None
        check(name + " -> reply", r == want if want is not None else r in (0, 1), r)
        if chk and i < len(regs):
            for reg, mask, exp in chk:
                check("   %s & %02x == %02x" % (reg, mask, exp), regs[i].get(reg, 0) & mask == exp, regs[i])
    check("the line is released after each reply",
          [m.group(1) for m in re.finditer(r"(bsf|bcf)\s+trisio,2", log)] == ["bcf", "bsf"] * len(STEPS))
    print("%d failure(s)" % fails)
    sys.exit(1 if fails else 0)

main()
