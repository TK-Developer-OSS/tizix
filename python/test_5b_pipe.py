#!/usr/bin/env python3
# test_5b_pipe.py - カーネルパイプ(両側外部コマンド)
#   ptx N | prx  ->  prx が "rx: line-K" を N 行 + "prx: EOF"
#   N を PBUF(128B)より大きい総量にして writer の block/wake も通す。
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


def read_until(pat, timeout=5.0):
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


def run_pipe(cmd, nlines, timeout=6.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("prx: EOF", timeout)
    out += read_until("]# ", 2.0)
    body = out.replace("\r", "")
    got = [ln for ln in body.split("\n") if ln.startswith("rx: ")]
    print(f"--- {cmd} ---\n{body}")
    return got, ("prx: EOF" in body)


# 小: バッファ内に収まる
got, eof = run_pipe("ptx 5 | prx", 5)
want = [f"rx: line-{i}" for i in range(5)]
if got != want:
    print(f"[FAIL] ptx 5: want {want} got {got}"); fails += 1
elif not eof:
    print("[FAIL] ptx 5: EOF 未達"); fails += 1
else:
    print("[OK ] ptx 5 | prx : 5 行 + EOF")

# 大: 総量 > PBUF(128B)。writer の proc_block/wake を通す
got, eof = run_pipe("ptx 40 | prx", 40, timeout=8.0)
want = [f"rx: line-{i}" for i in range(40)]
if got != want:
    print(f"[FAIL] ptx 40: {len(got)} 行 (want 40)  先頭={got[:3]} 末尾={got[-3:]}"); fails += 1
elif not eof:
    print("[FAIL] ptx 40: EOF 未達"); fails += 1
else:
    print("[OK ] ptx 40 | prx : 40 行 + EOF(writer block/wake 経由)")

# パイプ後もシェルは通常どおり
os.write(master, b"pwd\n")
p = read_until("]# ", 3.0)
if "/" not in p:
    print("[FAIL] パイプ後にシェルが壊れた"); fails += 1
else:
    print("[OK ] パイプ後もシェル健在")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
