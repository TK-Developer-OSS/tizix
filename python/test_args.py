#!/usr/bin/env python3
# test_args.py - 外部コマンドへの引数の渡り方がアーキ間で揃っているか(#48 / #81)
#
#   使い方: python3 python/test_args.py [z80pack|z80board|m68k-mega]
#   検査:
#     ・echo a b c / echo "x  y" z(引用符の中の空白を保つ)
#     ・wc -l FILE / wc -lw FILE / パイプ後段の wc -l(-l をファイル名と見ない)
#     ・cwd 相対の wc -l(フラグは絶対化されない)
#     ・rm A B(2 個目のパスも cwd 起点で解決される)
import os
import re
import sys
import pty
import time
import select
import subprocess

TIZIX = os.environ.get("TIZIX_ROOT", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARCH = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("TIZIX_ARCH", "z80pack")
SIMS = {
    "z80pack":   ["./cpmsim", "-d", "disks"],
    "z80board":  ["./z80boardsim", "-x", "obj/kernel.ihx"],
    "m68k-mega": ["./m68ksim", "obj/kernel.bin", "obj/disk.img"],
}

master, slave = pty.openpty()
proc = subprocess.Popen(SIMS[ARCH], cwd=os.path.join(TIZIX, "arch", ARCH),
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout):
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


def run(cmd):
    os.write(master, (cmd + "\r").encode())
    out = read_until("]# ", 20.0).replace("\r", "").split("\n")
    body = out[1:-1]
    print("--- " + cmd)
    print("\n".join(body))
    return body


fails = 0


def check(name, cond):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name)
    if not cond:
        fails += 1


read_until("]# ", 30.0)

check("echo a b c", run("echo a b c") == ["a b c"])
check('echo "x  y" z(引用符の中の空白を保つ)', run('echo "x  y" z') == ["x  y z"])

rc_lines = None
out = run("wc /etc/rc")
m = re.match(r"^(\d+) (\d+) (\d+) /etc/rc$", out[0] if out else "")
check("wc FILE = 3 欄 + 名前", m is not None)
if m:
    rc_lines, rc_words = m.group(1), m.group(2)
    check("wc -l FILE", run("wc -l /etc/rc") == ["%s /etc/rc" % rc_lines])
    check("wc -lw FILE", run("wc -lw /etc/rc") == ["%s %s /etc/rc" % (rc_lines, rc_words)])
    check("cat FILE | wc -l", run("cat /etc/rc | wc -l") == [rc_lines])
    check("echo | wc -l", run("echo hi | wc -l") == ["1"])
    # 4KB を超える入力(vi.bin は 7KB 超)。reader は最後まで読む tail にする
    # (wc は stdin の 0x04 を終端扱いで途中で終わり、writer が刈られて 4KB に届かない)。
    big = run("cat /bin/vi.bin | tail -1")
    check("4KB 超のパイプは打ち切ったと表示する",
          any("sh: pipe: truncated at 4KB" in l for l in big))
    check("cat FILE | head -1", run("cat /etc/rc | head -1") == [run("head -1 /etc/rc")[0]])
    check("cat FILE | tail -1(パイプ後段の tail)",
          run("cat /etc/rc | tail -1") == [run("tail -1 /etc/rc")[0]])
    run("cd /etc")
    rel = run("wc -l rc")
    check("cwd 相対の wc -l(フラグを絶対化しない)",
          len(rel) == 1 and re.match(r"^%s (/etc/)?rc$" % rc_lines, rel[0]) is not None)
    run("cd /root")

run("echo 1 > /root/ta.txt")
run("echo 2 > /root/tb.txt")
run("cd /root")
run("rm ta.txt tb.txt")
left = run("ls /root")
check("rm A B(2 個とも消える)", "TA.TXT" not in left and "TB.TXT" not in left
      and "ta.txt" not in left and "tb.txt" not in left)

os.write(master, b"\x1d")
time.sleep(0.3)
proc.kill()
print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
