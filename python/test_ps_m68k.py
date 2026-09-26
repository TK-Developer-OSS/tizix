#!/usr/bin/env python3
# test_ps_m68k.py - m68k-mega の ps が z80 と同じ BLK ST CMD ARGS 表示になるか(#78)
#
#   m68ksim を pty で起動し、背景ジョブを 2 本立てて ps を見る。
#     slot 0 = init(カーネル)、slot 1 = sh(/bin/sh.bin)
#     sleep 30 &   → slot 2 に "sleep 30"
#     sleep 20 &   → slot 3 に "sleep 20"(出力の無い背景ジョブ。a は A を打ち続けて
#                    ps の行に割り込むので使わない。100Hz 化 #83 で目立った)
#   kill 後の ps でプロセス行が消えることも確認する。
#   前提: make -C arch/m68k-mega 済み(obj/kernel.bin, obj/disk.img)。
import os
import sys
import pty
import time
import select
import subprocess

TIZIX = os.environ.get("TIZIX_ROOT", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARCH_DIR = os.path.join(TIZIX, "arch", "m68k-mega")

master, slave = pty.openpty()
proc = subprocess.Popen(
    ["./m68ksim", "obj/kernel.bin", "obj/disk.img"], cwd=ARCH_DIR,
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
    out = read_until("]# ", timeout)
    # 背景の a が 'A' を混ぜるので、行頭の A の連なりを落として比較する
    lines = [l.lstrip("A") for l in out.replace("\r", "").split("\n")]
    return lines


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + detail))
    if not cond:
        fails += 1


boot = read_until("]# ", 20.0)
print("=== boot ===")
print(boot)
check("boot prompt", boot.endswith("]# "), repr(boot[-80:]))

run("sleep 30 &")
run("sleep 20 &")
time.sleep(1.0)
ps1 = run("ps")
print("=== ps (2 jobs) ===")
print("\n".join(ps1))
check("header BLK ST CMD ARGS", "BLK ST CMD ARGS" in ps1, repr(ps1))
# sh は外部プロセス(/bin/sh.bin)なので slot 0 は init、sh は slot 1、ジョブは 2・3。
check("slot0 (init)", any(l.startswith("0 rdy (init)") for l in ps1), repr(ps1))
check("slot1 = sh(ps を実行中)", any(l.startswith("1 run sh") for l in ps1), repr(ps1))
check("sleep 30 with args",
      any(l.startswith("2 ") and l.endswith(" sleep 30") for l in ps1), repr(ps1))
check("2 本目の背景ジョブ",
      any(l.startswith("3 ") and l.rstrip().endswith(" sleep 20") for l in ps1), repr(ps1))

run("kill 2")
run("kill 3")
time.sleep(0.5)
ps2 = run("ps")
print("=== ps (after kill) ===")
print("\n".join(ps2))
check("header after kill", "BLK ST CMD ARGS" in ps2, repr(ps2))
check("no job lines after kill",
      not any(l[:2] in ("2 ", "3 ") for l in ps2), repr(ps2))
check("sh は残っている", any(l.startswith("1 run sh") for l in ps2), repr(ps2))

os.write(master, b"\x1d")                 # Ctrl+] で m68ksim を抜ける
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
