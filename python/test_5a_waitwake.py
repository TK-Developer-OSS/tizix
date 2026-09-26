#!/usr/bin/env python3
# test_5a_waitwake.py - スケジューラ wait/wake (proc_block / proc_wake)
#   blk &  : proc_block で自プロセスを park(戻ってこない)
#   ps     : シェルは生きている(スケジューラが停止しない証拠)、blk のブロックは残る
#   wak N  : proc_wake でそのブロックを起こす
#   -> blk が "woke, exit" を出して終了、ps から消える
import os
import sys
import pty
import time
import select
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

tzpaths.wait_disk_ready(tzpaths.DRIVEB)

master, slave = pty.openpty()
proc = subprocess.Popen(
    tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
    stdin=slave, stdout=slave, stderr=slave, close_fds=True,
)
os.close(slave)


def read_until(pat, timeout=4.0):
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


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 6.0)
print("=== boot ===\n" + boot)

fails = 0


def send(cmd):
    os.write(master, (cmd + "\n").encode())


import re

# 1) blk を背景起動 → "blk: parking block N" が出てプロンプトへ
send("blk &")
out = read_until("blk: parking block", 3.0)
out += read_until("# ", 2.0)
print("--- after 'blk &' ---\n" + out)
m = re.search(r"blk: parking block (\d+)", out)
if not m:
    print("[FAIL] blk が park メッセージを出していない"); fails += 1
    blk_blk = "2"
else:
    blk_blk = m.group(1)
    print(f"[OK ] blk: parking block {blk_blk} 出力")

time.sleep(0.5)

# 2) ps : シェルが応答する(=スケジューラ健在)。blk のブロックが残っている
send("ps")
ps1 = read_until("]# ", 3.0)
print("--- ps (blk parked) ---\n" + ps1)
if not re.search(rf"^{blk_blk}\s", ps1, re.M):
    print(f"[FAIL] ps に blk のブロック {blk_blk} が出ていない"); fails += 1
elif "BLK ST CMD ARGS" not in ps1 or not ps1.endswith("]# "):
    # 以前は "0   run (sh)"(sh がカーネル内蔵だった頃の ps 形式)を探していた。
    # sh は外部化されて block0 は idle(rdy)なので、ps が答えてプロンプトへ
    # 戻ったこと = シェル健在、で判定する(#79)。
    print("[FAIL] シェルが応答していない(スケジューラ停止?)"); fails += 1
else:
    print(f"[OK ] blk はブロック {blk_blk} で parked、シェル(block0)は応答")

# 3) wak <blk> で起こす → blk が woke, exit を出す
send(f"wak {blk_blk}")
w = read_until("blk: woke, exit", 3.0)
w += read_until("# ", 2.0)
print("--- after 'wak' ---\n" + w)
if "wak: block" not in w:
    print("[FAIL] wak が実行されていない"); fails += 1
if "blk: woke, exit" not in w:
    print("[FAIL] blk が起きて終了していない(wake 効かず)"); fails += 1
else:
    print("[OK ] blk が proc_wake で復帰して終了")

time.sleep(0.4)

# 4) ps : blk のブロックが消えている
send("ps")
ps2 = read_until("]# ", 3.0)
print("--- ps (after wake) ---\n" + ps2)
still = [ln for ln in ps2.splitlines()
         if ln[:1].isdigit() and ln.split()[0] == blk_blk]
if still:
    print(f"[FAIL] ブロック {blk_blk} がまだ残っている"); fails += 1
else:
    print(f"[OK ] ブロック {blk_blk} は解放された")

send("exit")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
