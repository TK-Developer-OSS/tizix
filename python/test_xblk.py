#!/usr/bin/env python3
# test_xblk.py - 追加ブロック(像に含めない作業領域) (#38)
#
#   .BIN 先頭 32B の予約ヘッダ(crt0 の `.ds 0x20` = 従来まったくの死に領域)に
#   「像とは別に欲しいブロック数」を刻んでおくと、kexec がそのぶん多く確保する。
#   大きな配列を像に持つと .BIN が実データの無いゼロで太り、ロードも遅く、
#   4KB 単位の切り上げで端数が無駄になる ── それを避けるための仕組み。
#
#   確かめること:
#     (1) ヘッダの宣言どおり追加ブロックが確保される
#     (2) 追加ブロックは自分のブロックより上にある(像を侵していない)
#     (3) 4KB 全域に書いて読み返せる(SP や argv[] と衝突していない)
#     (4) 宣言していないコマンドは従来どおり動く(回帰は他のテストが担保)
import os
import sys
import time
import pty
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


def run(cmd, timeout=30.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]
    return "\n".join(lines[1:]).strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
print("=== boot ===\n" + boot)

fails = 0


def check(desc, ok, extra=""):
    global fails
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] {desc}  {extra}")


got = run("xblk")
print("--- xblk ---\n" + got)
lines = [L.strip() for L in got.splitlines()]

diag = ""
for L in lines:
    if L.startswith("xblk: base="):
        diag = L
        break

base = xbase = xsize = 0
if diag:
    try:
        kv = dict(p.split("=") for p in diag.replace("xblk: ", "").split())
        kv["xsize"] = kv.get("usable", kv.get("xsize", "0"))
        base = int(kv["base"])
        xbase = int(kv["xbase"])
        xsize = int(kv["xsize"])
    except Exception:
        pass

check("追加ブロックが確保され使える大きさがある", xsize >= 2048, f"-> {diag}")
check("追加ブロックは自分のブロックより上にある", base != 0 and xbase > base,
      f"-> base={base} xbase={xbase}")
check("像の直後の 4KB 境界にある",
      xbase != 0 and (xbase - base) % 4096 == 0, f"-> 差 {xbase - base}")
check("4KB 全域を書いて読み返せた(SP/argv と衝突していない)",
      any("bad=0" in L for L in lines))
check("OK を返した", any(L == "xblk: OK" for L in lines))

# 連続実行(追加ブロックも解放されている)
got2 = run("xblk")
check("連続実行できる(追加ブロックが解放されている)", "xblk: OK" in got2)

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
