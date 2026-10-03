#!/usr/bin/env python3
# test_cmds_all.py - 小さなコマンドを一通り起動する(スモーク)+ df / uptime(#56)
#   以前は結果を見ずに走らせるだけで、しかも cpmsim 決め打ちだった
#   (TIZIX_ARCH=z80board でも z80pack を起動していた)。tzpaths を使い、
#   外部化した df / uptime は出力の形を確かめる。
import os
import re
import select
import subprocess
import sys
import time
import pty

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

tzpaths.wait_disk_ready(tzpaths.DRIVEB)

master, slave = pty.openpty()
proc = subprocess.Popen(
    tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
    stdin=slave, stdout=slave, stderr=slave, close_fds=True,
)
os.close(slave)


def read_until(pat, timeout=5.0):
    buf = b""
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 1024)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                return buf
    return buf


print("Waiting for boot...")
out = read_until("]# ", 20.0)
print(out.decode(errors='ignore'))

fails = 0


def run_cmd(cmd, timeout=6.0):
    print(f"\n--- Running: {cmd} ---")
    os.write(master, (cmd + "\n").encode())
    res = read_until("]# ", timeout).decode(errors='ignore')
    print(res)
    lines = res.replace("\r", "").split("\n")
    return "\n".join(lines[1:-1]).strip("\n")


def check(desc, ok, got):
    global fails
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] {desc:<36} -> {got!r}")


run_cmd("hello")
# a / b は「kill されるまで 0.5 秒ごとに A/B を出し続ける」マルチタスクの
# 実演用スクラッチ。前景で走らせると戻ってこず、後続のコマンドが届かない
# (以前のこのテストは結果を見ていなかったので気付かれていなかった)。
run_cmd("date")

got = run_cmd("uptime")
check("uptime = 'up N sec'", re.fullmatch(r"up \d+ sec", got) is not None, got)

got = run_cmd("df")
m = re.fullmatch(r"total (\d+)KB free (\d+)KB", got)
check("df = 'total NKB free NKB'", m is not None, got)
if m:
    check("df: 0 < free <= total", 0 < int(m.group(2)) <= int(m.group(1)), got)

#   free(#64): sh(3 枠)+ free 自身(1 枠)が居る状態 = used 4 / free 2 / 連続空き 2。
#   起動時メモリチェックで不良が出れば 'x' が混ざり、ここが崩れる。
got = run_cmd("free")
if tzpaths.ARCH == "m68k-mega":
    # m68k: 32KB スロット 1..30。sh(slot 1)+ free 自身の 2 枠。
    check("free: used 2 free 28 run 28",
          "used 2 (64KB)  free 28 (896KB)  largest free run 28 (896KB)" in got, got)
    check("free: 見出し", "slots 1-30: 30 x 32KB = 960KB" in got, got)
elif tzpaths.ARCH == "esp32-wroom-32e":
    # esp32: スロット 1..5、1 枠 32KB(命令 16 + データ 16)。sh(slot 1)+ esp32d(/etc/rc)+ free 自身の 3 枠。
    check("free: used 3 free 2 run 2",
          "used 3 (96KB)  free 2 (64KB)  largest free run 2 (64KB)" in got, got)
    check("free: 見出し", "slots 1-5: 5 x 32KB = 160KB" in got, got)
else:
    check("free: used 4 free 2 run 2",
          "used 4 (16KB)  free 2 (8KB)  largest free run 2 (8KB)" in got, got)
check("free: 不良ブロック(x)が無い", "x" not in got.split("\n")[-1], got)

proc.terminate()
print(f"=== {'ALL PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
