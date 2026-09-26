#!/usr/bin/env python3
# test_sh_hist.py - sh のコマンドヒストリ(#45)
#   ↑(ESC [ A)/ ↓(ESC [ B)で /root/history のリングから行を呼び出す。
#   検証は「呼び出した行を実行した結果」と「画面に出た行」の両方で見る。
#   存在しないコマンド名(xA 等)を使う ── 起動が速く、出力 "sh: xA: not found"
#   で何が実行されたか判る。
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

UP = "\x1b[A"
DN = "\x1b[B"


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


def render(s):
    """1 行ぶんの端末出力を BS を効かせて描き直す(rubout の "\\b \\b" を畳む)。"""
    out = []
    cur = 0
    for ch in s:
        if ch == "\b":
            cur = max(0, cur - 1)
        elif ch == "\a":
            continue
        else:
            if cur < len(out):
                out[cur] = ch
            else:
                out.append(ch)
            cur += 1
    return "".join(out).rstrip(" ")


def send(keys, timeout=8.0):
    """keys を送り、次のプロンプトまで読む。戻り (画面上の入力行, 出力)。"""
    os.write(master, keys.encode("latin-1"))
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]
    first = render(lines[0]) if lines else ""
    return first, "\n".join(lines[1:]).strip("\n")


def run(cmd, timeout=6.0):
    return send(cmd + "\n", timeout)[1]


def keys_slow(seq):
    """矢印を 1 個ずつ送る(ESC と '[' の間が 100ms を越えないよう 1 write 1 キー)。"""
    for k in seq:
        os.write(master, k.encode("latin-1"))
        time.sleep(0.05)


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
print("=== boot ===\n" + boot)

fails = 0


def check(desc, got, want):
    global fails
    ok = (got == want)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] {desc:<44} -> {got!r:<34} (want {want!r})")


def nf(name):
    return f"sh: {name}: not found"


# ★記録は「実行の**後**」(user/sh.c: SD 書き込みで出力を待たせないため)。
#   よって `rm /root/history` の行は消した後に書かれ、**最古の 1 件として残る**。
#   以前このテストは「書いてから実行」前提で、↑ で最古まで遡って Enter すると
#   テスト自身が rm を実行して履歴を消し、以降が連鎖的に FAIL していた(#68)。
#   最古に触る検査は ^C で行を捨てて、実行しない。

# ---- 準備: 前回の実行分を消す(この rm 行は最古として残る)----
run("rm /root/history")
run("xA")
run("xB")
run("xC")

# ---- ↑ 1 回 = 直前 ----
keys_slow([UP])
line, out = send("\r")
check("↑ で直前の行", out, nf("xC"))
check("  画面の入力行", line, "xC")
# H = rm xA xB xC xC

# ---- ↑ 4 回 = 4 個前 ----
keys_slow([UP] * 4)
line, out = send("\r")
check("↑×4 で 4 個前", out, nf("xA"))
check("  画面の入力行(差し替えの残骸が無い)", line, "xA")
# H = rm xA xB xC xC xA

# ---- ↑ ↓ = 新規行(空)に戻る。空行は積まない ----
keys_slow([UP, DN])
line, out = send("\r")
check("↑↓ で空行に戻る", out, "")
check("  画面の入力行", line, "")

# ---- 件数を越えて ↑ しても最古で止まる(最古 = rm 行。実行せず ^C で捨てる)----
keys_slow([UP] * 20)
line, out = send("\x03")
check("↑×20 は最古(rm 行)で止まる", line, "rm /root/history")
# H = rm xA xB xC xC xA(^C の空行は積まない)

# ---- ↑↑ ↓ = 1 個戻る ----
keys_slow([UP] * 5 + [DN])
line, out = send("\r")
check("↑×5 ↓ で 4 個前", out, nf("xB"))
# H = rm xA xB xC xC xA xB

# ---- 呼び出した行は編集できる(BS + 打ち足し)----
keys_slow([UP])
line, out = send("\bZ\r")
check("↑ + BS + Z", out, nf("xZ"))
check("  画面の入力行", line, "xZ")

# ---- 打ちかけの行を ↑ で差し替える ----
os.write(master, b"hello")
time.sleep(0.2)
keys_slow([UP])
line, out = send("\r")
check("打ちかけ 'hello' を ↑ で差し替え", out, nf("xZ"))
check("  画面の入力行", line, "xZ")

# ---- sh を抜けても(init が respawn)ヒストリは残る ----
send("exit\n", 10.0)
keys_slow([UP, UP])
line, out = send("\r")
check("exit → respawn 後も ↑↑ で xZ", out, nf("xZ"))

# ---- history コマンド(#46): 1 区間(ローテーション前)----
run("rm /root/history")
run("xA")
run("xB")
out = run("history")
#   history 自身は実行の後に積まれるので、自分の出力には出ない。
check("history(1 区間)", out, "  1  rm /root/history\n  2  xA\n  3  xB")

# ---- 100 件でローテーション ----
run("rm /root/history")
for i in range(105):
    run(f"r{i}")
# 保持は r5..r104 の 100 件
keys_slow([UP] * 100)
line, out = send("\r", 15.0)
check("105 件積んで ↑×100 = r5(r0..r4 は捨て)", out, nf("r5"))
# 保持は r6..r104, r5
keys_slow([UP] * 100)
line, out = send("\r", 15.0)
check("続けて ↑×100 = r6", out, nf("r6"))
keys_slow([UP] * 110)
line, out = send("\r", 15.0)
check("↑×110 は 100 件目で止まる = r7", out, nf("r7"))

# ---- ファイルは 2 + 100×48 = 4802B 固定 ----
out = run("ls -l /root")
print("--- ls -l /root ---\n" + out)
ok = "4802" in out
if not ok:
    fails += 1
print(f"[{'OK ' if ok else 'FAIL'}] /root/history は 4802B で頭打ち")

# ---- history コマンド: ローテーション後(2 区間 [hhead,100) → [0,hhead))----
# 保持は r10..r104, r5, r6, r7, "ls -l /root", "history" の 100 件。
# hhead=10 なので古い側はスロット 10(= r10)から始まる。
out = run("history", 15.0)
hl = out.split("\n")
check("history 件数(ローテーション後)", len(hl), 100)
#   実行後に記録なので: rm がスロット 0、r0..r98 が 1..99、r99..r104 が 0..5、
#   r5 r6 r7 "ls -l /root" が 6..9 → hhead = 10。history は自分を含まない。
check("  先頭 = 最古", hl[0] if hl else "", "  1  r9")
# スロット 10..99 = r9..r98(90 件)→ 0..5 = r99..r104 → 6..9 = r5 r6 r7 ls
check("  90 行目 = r98(スロット 99 = 1 区間目の末尾)", hl[89] if len(hl) > 89 else "", " 90  r98")
check("  91 行目 = r99(スロット 0 = 2 区間目の先頭)", hl[90] if len(hl) > 90 else "", " 91  r99")
check("  97 行目 = r5", hl[96] if len(hl) > 96 else "", " 97  r5")
check("  末尾 = 直前の ls", hl[-1] if hl else "", "100  ls -l /root")

proc.kill()
print(f"=== {'ALL PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
