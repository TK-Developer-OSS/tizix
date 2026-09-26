#!/usr/bin/env python3
# test_ovl.py - オーバーレイ(自前で半ロード)の基盤 (#36)
#
#   #35 の「子プロセスへ出す」方式は、実行の瞬間に空きブロックが 1 個要る。
#   オーバーレイは **ブロックを 1 個も余分に食わない** ── 自分の空間の空き番地へ
#   コード片を読み込んで呼ぶだけ。確かめること:
#     (1) 自分の空間へ .bin を fread できる
#     (2) 実行時に決まる番地を呼べる(tzcc は関数ポインタ非対応 → callovl)
#     (3) オーバーレイ内の IY 相対 PIC が親の IY で正しく解決される
#         = リンク番地と読み込み番地が一致していれば動く
#     (4) オーバーレイが親のバッファを更新できる
#     (5) 読み直さずに 2 回目を呼べ、オーバーレイ内の状態が保たれる
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


got = run("ovlmain")
print("--- ovlmain ---\n" + got)
lines = [L.strip() for L in got.splitlines()]


def field(prefix):
    for L in lines:
        if L.startswith(prefix):
            return L
    return ""


base_line = field("ovl: base=")
load_line = field("ovl: loaded")
rc_line = field("ovl: rc=")
buf_line = field("ovl: buf=")
rc2_line = field("ovl: rc2=")

# (1) 読み込めた
check("オーバーレイを自分の空間へ読み込めた", "loaded" in load_line and " 0 bytes" not in load_line,
      f"-> {load_line}")

# 読み込み先が「親のベース + リンク番地」であること。ここがずれると
# 親の別の場所を壊すので、番地そのものを突き合わせる。
ok_at = False
if base_line:
    try:
        kv = dict(p.split("=") for p in base_line.replace("ovl: ", "").split())
        ok_at = int(kv["load-at"]) == int(kv["base"]) + 0x1600
    except Exception:
        ok_at = False
check("読み込み先 = 親のベース + リンク番地(0x1600)", ok_at, f"-> {base_line}")

# (2) 実行時に決まる番地を呼べて、戻り値が返る
check("callovl で呼べて戻り値が返る(rc=1235 = core が +1 した値)", "1235" in rc_line, f"-> {rc_line}")

# #36 続き: オーバーレイ → core 呼び出し(ovlvec.s の固定番地サンク)。
#   この行は core 側の prs/prnum が出している。オーバーレイは両方とも
#   持っていないので、出れば「core のヘルパーを呼べた」ことの証拠になる。
check("オーバーレイが core の関数を呼べた(ovl_svc 経由)", "host: ovl_svc arg=1234" in got,
      f"-> {field('host: ovl_svc ')}")

# (3)(4) オーバーレイが自分の static を読み、親のバッファへ書けた
#   OVL-OK は overlay 自身の tag[] 由来 = IY 相対のデータ参照が効いている証拠
check("オーバーレイが自分の static を読み、親のバッファを更新した",
      "OVL-OK" in buf_line, f"-> {buf_line}")

# (5) 読み直さずに 2 回目が呼べる
check("読み直さずに 2 回目を呼べる", "1235" in rc2_line, f"-> {rc2_line}")

# 連続実行(プロセスとして起動し直しても壊れない)
got2 = run("ovlmain")
check("連続実行できる", "OVL-OK" in got2 and "1235" in got2)

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
