#!/usr/bin/env python3
# test_spawn.py - プロセス分割の基盤 (#35)
#
#   確かめること:
#     (1) sh 以外の普通のコマンドから子プロセスを起動できる (krun_wait)
#     (2) 子の終了を待って親が続きを実行できる
#     (3) **メモリ保護が無いので、子が親のバッファを絶対アドレスで直接書ける**
#   (3) が「vi を本体 + コマンドに割る」構成の成立条件。
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


def run(cmd, timeout=10.0):
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


# ---- 既定の語で往復 ----
got = run("spawn")
print("--- spawn ---\n" + got)
lines = [L.strip() for L in got.splitlines()]

check("親が起動して buf before を出す",
      any(L.startswith("spawn: buf before = parent") for L in lines))
check("子が起動して自分の出力を出した(= krun_wait が子を走らせた)",
      any(L.startswith("spawnc:") and "wrote" in L for L in lines))
check("親が子の終了後に再開した(= krun_wait が待った)",
      any(L.startswith("spawn: buf after") for L in lines))

after = [L for L in lines if L.startswith("spawn: buf after")]
check("★子が親のバッファを書き換えた(保護なし共有メモリ)",
      bool(after) and "CHILD-WAS-HERE" in after[0],
      f"-> {after[0] if after else '(no line)'}")

# 配列アドレスが絶対番地か、かつ「直接評価」と「変数経由」で一致するか。
#   ここがズレると、子に渡した番地が別プロセス/カーネル領域を指してしまう。
diag = [L for L in lines if L.startswith("spawn: base=")]
ok_abs = False
if diag:
    kv = dict(p.split("=") for p in diag[0].replace("spawn: ", "").split())
    base = int(kv.get("base", "0"))
    direct = int(kv.get("direct", "0"))
    viavar = int(kv.get("viavar", "0"))
    # 契約は「引数の位置で評価した配列アドレスは絶対番地」。ここが崩れたら
    # プロセス分割そのものが成立しないので、それを PASS 条件にする。
    ok_abs = (base <= direct < base + 0x4000) and base >= 0xA000
check("配列アドレス(引数の位置)は絶対番地でプロセス空間内",
      ok_abs, f"-> {diag[0] if diag else '(no line)'}")

# tzcc の既知バグ(#35): 配列アドレスを変数へ代入すると +IY されない。
# 直ったら教えてほしいので、直っていたら目立つように出す(FAIL にはしない)。
if diag:
    if direct == viavar:
        print("[NOTE] tzcc の『配列アドレスを変数へ代入すると +IY されない』バグは "
              "直っているようです(direct == viavar)。task.md #35 を更新してください")
    else:
        print(f"[KNOWN] tzcc bug: 変数経由だと +IY されない "
              f"(direct={direct} viavar={viavar})。`unsigned a = (unsigned)arr;` と書かないこと")

# 子の書込み先が親のブロック内であること(自分の block を壊していない)
addr_c = [L for L in lines if L.startswith("spawnc:")]
ok_addr = False
if diag and addr_c:
    ok_addr = str(int(kv.get("direct", "0"))) in addr_c[0]
check("子の書込み先が親のバッファ番地と一致する",
      ok_addr, f"-> {addr_c[0] if addr_c else '(no line)'}")

# ---- 引数で語を指定 ----
got2 = run("spawn HELLO")
check("引数で渡した語が親のバッファに入る",
      any(L.startswith("spawn: buf after") and "HELLO" in L for L in got2.splitlines()))

# ---- 連続実行してブロックが解放されているか ----
got3 = run("spawn AGAIN")
check("連続実行できる(子ブロックが解放されている)",
      any("AGAIN" in L for L in got3.splitlines()))

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
