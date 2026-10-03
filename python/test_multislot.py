#!/usr/bin/env python3
# test_multislot.py - 複数スロットのプロセス(#113、esp32-wroom-32e)
#
#   使い方: python3 python/test_multislot.py [esp32-wroom-32e]
#   bigbss(user/bigbss.c)は .bss が 20000 バイトで 16KB の枠に入らないので、ローダーが連続した
#   2 スロットを取る。
#     ・全部書いて読み返せる
#     ・走っている間 free の使用が 2 増え、ps には先頭の 1 行だけ出る
#     ・終われば 2 つとも空く / kill(先頭の番号)でも 2 つとも空く / 続きの番号では kill できない
import os
import re
import sys
import pty
import time
import select
import subprocess

os.environ["TIZIX_ARCH"] = sys.argv[1] if len(sys.argv) > 1 else "esp32-wroom-32e"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

master, slave = pty.openpty()
proc = subprocess.Popen(
    tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
    stdin=slave, stdout=slave, stderr=slave, close_fds=True,
)
os.close(slave)


def read_until(pat, timeout=8.0):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 4096)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                break
    return buf.decode("latin-1", "replace")


def run(cmd, timeout=8.0):
    os.write(master, (cmd + "\r").encode())
    out = read_until("]# ", timeout).replace("\r", "")
    print("$ " + cmd + "\n" + out)
    return out


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + repr(detail)))
    if not cond:
        fails += 1


def used(out):
    m = re.search(r"used (\d+)", out)
    return int(m.group(1)) if m else -1


def slot_of(out, name):
    for l in out.split("\n"):
        m = re.match(r"(\d+) \S+ " + name + r"\b", l)
        if m:
            return int(m.group(1))
    return -1


boot = read_until("]# ", 30.0)
check("boot prompt", boot.endswith("]# "), boot[-80:])

base = used(run("free"))
out = run("bigbss")
check("20000 バイトを書いて読み返せる", "bigbss: 20000 ok" in out, out)
check("終われば元に戻る", used(run("free")) == base, base)

run("bigbss 30 &")
time.sleep(1.0)
out = run("free")
check("走っている間は 2 スロット使う", used(out) == base + 2, out)
ps = run("ps")
n = slot_of(ps, "bigbss")
check("ps には先頭の 1 行", n > 0 and ps.count("bigbss") == 1, ps)
out = run("kill %d" % (n + 1))
check("続きの番号では kill できない", "not running" in out, out)
run("kill %d" % n)
time.sleep(0.5)
check("先頭を kill すれば 2 つとも空く", used(run("free")) == base, base)

proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
