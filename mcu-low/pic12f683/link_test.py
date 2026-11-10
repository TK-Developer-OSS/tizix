#!/usr/bin/env python3
"""The link and the virtual machine together (vm.asm), driven like a host: frames on GP2 at 500 baud.
usage: link_test.py   (needs gpasm and gpsim; vm_test.py builds the EEPROM image first)"""
import os, re, subprocess, sys
os.chdir(os.path.dirname(os.path.abspath(__file__)))
B, START, GAP = 2000, 30000, 130000
# (name, bytes to send, expected reply)
STEPS = [("write node 0 = 1",            [0x80, 1],    0x00),
         ("set the EEPROM pointer 0x40", [0xA0, 0x40], 0x00),
         ("write EEPROM data 0x5A",      [0xA1, 0x5A], 0x00),
         ("set the pointer again",       [0xA0, 0x40], 0x00),
         ("read the EEPROM back",        [0x21],       0x5A),
         ("read the output ring: 'R'",   [0x22],       0x52),   # vm_test's last program prints READYHI
         ("read the output ring: 'E'",   [0x22],       0x45),
         ("read control: running",       [0x23],       0x00),
         ("write control = stop",        [0xA3, 0],    0x00),
         ("read control: stopped",       [0x23],       0x01),
         ("refuse a write to GP3",       [0x84, 1],    0xFF),
         ("refuse node 9",               [0x89, 1],    0xFF)]
pairs, last = [], 1
for n, (_, send, _) in enumerate(STEPS):
    t = START + n * GAP
    for byte in send:
        for b in [0] + [(byte >> i) & 1 for i in range(8)] + [1]:
            if b != last: pairs.append((t, b)); last = b
            t += B
        t += 3000
end = START + GAP * len(STEPS) + 5000
stc = "load s vm.cod\nstimulus asynchronous_stimulus\ninitial_state 1\nstart_cycle 0\ndigital\n{ %s }\nname host\nend\n" % \
      ", ".join("%d,%d" % p for p in pairs)
stc += "node n_line\nattach n_line host p12f683.gpio2\nlog on link.log\nlog w gpio\nbreak c %d\nrun\nx gpio\nquit\n" % end
open("link_run.stc", "w").write(stc)
out = subprocess.run(["gpsim", "-i", "-c", "link_run.stc"], check=True, timeout=900, capture_output=True, text=True).stdout
log = open("link.log").read()
ev, level = [(0, 1)], 1
for m in re.finditer(r"^0x([0-9A-F]+) p12f683 \S+ \S+ (bsf|bcf)\s+gpio,2", log, re.M):
    lv = 1 if m.group(2) == "bsf" else 0
    if lv != level: ev.append((int(m.group(1), 16), lv)); level = lv
def at(t):
    v = 1
    for c, l in ev:
        if c <= t: v = l
    return v
seen = []
for c, l in ev:
    if l == 0 and at(c - 1) == 1 and (not seen or c - seen[-1] > 30000): seen.append(c)
replies = [sum(at(c + B // 2 + B * (k + 1)) << k for k in range(8)) for c in seen]
fails = 0
def check(name, cond, got=None):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
    fails += not cond
check("one reply per command", len(replies) == len(STEPS), [hex(r) for r in replies])
for i, (name, _, want) in enumerate(STEPS):
    check(name, i < len(replies) and replies[i] == want, hex(replies[i]) if i < len(replies) else None)
gp = int(re.findall(r"gpio\[0x5\] = 0x([0-9a-f]+)", out)[-1], 16)
check("GP0 is high (node 0 = 1)", gp & 1 == 1, hex(gp))
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
