#!/usr/bin/env python3
# test_vi.py - 外部コマンド vi (#33)
#   フルスクリーンなので画面文字列ではなく「編集結果のファイル内容」で検証する。
#   ・vi FILE で入り、キーを送り、:wq / :q! で抜け、cat で結果を見る。
#   ・描画が出ているか(チルダ行 / ステータス行)は起動直後の 1 回だけ確認する。
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


def drain(t=0.4):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < t:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 4096)
            except OSError:
                break
            if not c:
                break
            buf += c
            t0 = time.time()          # 出力が続く間は待ち直す
    return buf.decode("latin-1", "replace")


def run(cmd, timeout=6.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]
    return "\n".join(lines[1:]).strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
print("=== boot ===\n" + boot)

fails = 0


def check(desc, got, want):
    global fails
    ok = (got == want)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] {desc:<40} -> {got!r:<30} (want {want!r})")


def vi_session(keys, tmo=8.0, show=False):
    """vi VI.TXT を起動し keys を送って抜ける。戻りは起動直後の画面。"""
    os.write(master, b"vi VI.TXT\n")
    screen = drain(2.0)
    if show:
        print("--- screen ---")
        print(screen.replace("\x1b", "<ESC>")[:1200])
    for k in keys:
        os.write(master, k.encode("latin-1"))
        time.sleep(0.25)
    read_until("]# ", tmo)
    return screen


# ---- 準備 ----
run("rm VI.TXT")
run("echo abcd > VI.TXT")

# ---- 起動して何もせず :q ----
scr = vi_session([":q\r"], show=True)
ok_draw = ("~" in scr) and ("VI.TXT" in scr) and ("\x1b[" in scr)
if not ok_draw:
    fails += 1
print(f"[{'OK ' if ok_draw else 'FAIL'}] 起動時に画面を描く(~ / ファイル名 / ANSI)")
check("何もせず :q なら中身不変", run("cat VI.TXT"), "abcd")

# ---- x で 1 文字消して :wq ----
vi_session(["x", ":wq\r"])
check("x + :wq", run("cat VI.TXT"), "bcd")

# ---- 行末追記($ + a。A は使わないとのことで外した)----
vi_session(["$", "a", "XY", "\x1b", ":wq\r"])
check("$ + a で行末追記", run("cat VI.TXT"), "bcdXY")

# ---- i で行頭挿入 ----
vi_session(["i", "Z", "\x1b", ":wq\r"])
check("i で挿入", run("cat VI.TXT"), "ZbcdXY")

# ---- o で下に 1 行 ----
vi_session(["o", "line2", "\x1b", ":wq\r"])
check("o で下に行追加", run("cat VI.TXT"), "ZbcdXY\nline2")

# ---- j / dd ----
vi_session(["j", "d", "d", ":wq\r"])
check("j + dd で 2 行目削除", run("cat VI.TXT"), "ZbcdXY")

# ---- :q! は保存しない ----
vi_session(["x", "x", ":q!\r"])
check(":q! は保存しない", run("cat VI.TXT"), "ZbcdXY")

# ---- :q は変更ありだと抜けられない → :q! で脱出 ----
vi_session(["x", ":q\r", ":q!\r"])
check(":q 拒否 -> :q! で脱出", run("cat VI.TXT"), "ZbcdXY")

# ---- 新規ファイル ----
run("rm VI2.TXT")
os.write(master, b"vi VI2.TXT\n")
drain(2.0)
for k in ["i", "new file body", "\x1b", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.25)
read_until("]# ", 8.0)
check("新規ファイルを作れる", run("cat VI2.TXT"), "new file body")

# ---- 複数行ファイルを開いて 2 行目を編集 ----
run("rm VI3.TXT")
run("echo one > VI3.TXT")
os.write(master, b"vi VI3.TXT\n")
drain(2.0)
for k in ["$", "a", "!", "\x1b", "o", "two", "\x1b", "o", "three", "\x1b", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.25)
read_until("]# ", 8.0)
check("3 行に育てる", run("cat VI3.TXT"), "one!\ntwo\nthree")

# ---- カーソルキー(ESC [ A/B) ----
os.write(master, b"vi VI3.TXT\n")
drain(2.0)
for k in ["\x1b[B", "\x1b[B", "x", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("下カーソル 2 回 + x", run("cat VI3.TXT"), "one!\ntwo\nhree")


# ---- 上へ戻る: k と カーソル上キー(ESC [ A)----
#   下(ESC [ B)は上でテスト済み。上だけ落ちていないかを別々に見る。
def make3(name):
    run("rm " + name)
    run("echo r1 > " + name)
    os.write(master, ("vi " + name + "\n").encode())
    drain(2.0)
    for k in ["o", "r2", "\x1b", "o", "r3", "\x1b", ":wq\r"]:
        os.write(master, k.encode("latin-1"))
        time.sleep(0.25)
    read_until("]# ", 8.0)

make3("VIB.TXT")
os.write(master, b"vi VIB.TXT\n")
drain(2.0)
for k in ["j", "j", "k", "x", ":wq\r"]:      # 3 行目まで下りて 1 行戻る
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("j j k で 2 行目へ戻り x", run("cat VIB.TXT"), "r1\n2\nr3")

make3("VIC.TXT")
os.write(master, b"vi VIC.TXT\n")
drain(2.0)
for k in ["\x1b[B", "\x1b[B", "\x1b[A", "x", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.35)
read_until("]# ", 8.0)
check("下下上カーソルで 2 行目へ戻り x", run("cat VIC.TXT"), "r1\n2\nr3")

#   1 行目で更に上 = 何も起きない(位置も動かない)
run("rm VID.TXT")
run("echo abc > VID.TXT")
os.write(master, b"vi VID.TXT\n")
drain(2.0)
for k in ["l", "\x1b[A", "x", ":wq\r"]:      # 2 桁目で上 -> 動かず 'b' が消える
    os.write(master, k.encode("latin-1"))
    time.sleep(0.35)
read_until("]# ", 8.0)
check("1 行目で上カーソルは何もしない", run("cat VID.TXT"), "ac")

# ---- カウント接頭辞 (3dd) ----
#   本家 vi の「回数 + コマンド」。dd / yy に効く(移動には付けていない)。
run("rm VI5.TXT")
run("echo L1 > VI5.TXT")
os.write(master, b"vi VI5.TXT\n")
drain(2.0)
for k in ["o", "L2", "\x1b", "o", "L3", "\x1b", "o", "L4", "\x1b", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.25)
read_until("]# ", 8.0)
check("4 行を作る", run("cat VI5.TXT"), "L1\nL2\nL3\nL4")

os.write(master, b"vi VI5.TXT\n")
drain(2.0)
for k in ["j", "2", "d", "d", ":wq\r"]:      # 2 行目から 2 行削除
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("2dd で 2 行削除", run("cat VI5.TXT"), "L1\nL4")

# ---- gg(先頭へ)----
#   以前は cur だけ動いて画面が更新されず「効かない」ように見えていた。
#   カーソルが 1 行目へ戻っていることを、そこで x して確かめる。
run("rm VI6.TXT")
run("echo aaa > VI6.TXT")
os.write(master, b"vi VI6.TXT\n")
drain(2.0)
for k in ["o", "bbb", "\x1b", "o", "ccc", "\x1b", "g", "g", "x", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("gg で先頭行へ戻り x が効く", run("cat VI6.TXT"), "aa\nbbb\nccc")


# ---- 行番号ジャンプ(N G / N gg)と G ----
#   カウント接頭辞は dd / yy 専用だったので #44 で G / gg にも効かせた。
#   dc_n は「0 = 数字を打っていない」で持つ(`1G` と素の `G` を区別するため)。
run("rm VIE.TXT")
run("echo n1 > VIE.TXT")
os.write(master, b"vi VIE.TXT\n")
drain(2.0)
for k in ["o", "n2", "\x1b", "o", "n3", "\x1b", "o", "n4", "\x1b", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.25)
read_until("]# ", 8.0)
check("4 行を作る(行番号ジャンプ用)", run("cat VIE.TXT"), "n1\nn2\nn3\nn4")

os.write(master, b"vi VIE.TXT\n")
drain(2.0)
for k in ["3", "G", "x", ":wq\r"]:            # 3 行目の先頭へ
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("3G で 3 行目へ", run("cat VIE.TXT"), "n1\nn2\n3\nn4")

os.write(master, b"vi VIE.TXT\n")
drain(2.0)
for k in ["2", "g", "g", "x", ":wq\r"]:       # 2 行目の先頭へ
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("2gg で 2 行目へ", run("cat VIE.TXT"), "n1\n2\n3\nn4")

os.write(master, b"vi VIE.TXT\n")
drain(2.0)
for k in ["G", "x", ":wq\r"]:                 # 素の G = 最終行
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("素の G は最終行へ", run("cat VIE.TXT"), "n1\n2\n3\n4")

os.write(master, b"vi VIE.TXT\n")
drain(2.0)
for k in ["G", "9", "9", "G", "x", ":wq\r"]:  # 行数を超えた指定 = 最終行で止まる
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("行数を超える 99G は最終行", run("cat VIE.TXT"), "n1\n2\n3")

# ---- e(語末へ)----
run("rm VI7.TXT")
run("echo ab cd > VI7.TXT")
os.write(master, b"vi VI7.TXT\n")
drain(2.0)
for k in ["e", "x", ":wq\r"]:                # 'ab' の語末 'b' を消す
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("e で語末へ移動して x", run("cat VI7.TXT"), "a cd")


# ---- undo(u。コマンド単位で 4 段)----
#   記録は追加ブロックの末尾 272B(68B x 4)。像は 1 バイトも使っていない。
run("rm VI8.TXT")
run("echo abcdef > VI8.TXT")
os.write(master, b"vi VI8.TXT\n")
drain(2.0)
for k in ["x", "x", "x", "u", "u", "u", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("x 3 回 -> u 3 回で元通り", run("cat VI8.TXT"), "abcdef")

#   4 段を超えたぶんは落ちる(5 回消して 5 回戻すと 1 文字ぶん戻らない)
os.write(master, b"vi VI8.TXT\n")
drain(2.0)
for k in ["x", "x", "x", "x", "x", "u", "u", "u", "u", "u", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.3)
read_until("]# ", 8.0)
check("5 回消して 5 回 u = 4 段ぶんだけ戻る", run("cat VI8.TXT"), "bcdef")

#   dd の取り消し(行まるごと)
run("rm VI9.TXT")
run("echo L1 > VI9.TXT")
os.write(master, b"vi VI9.TXT\n")
drain(2.0)
for k in ["o", "L2", "\x1b", "o", "L3", "\x1b", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.25)
read_until("]# ", 8.0)
os.write(master, b"vi VI9.TXT\n")
drain(2.0)
for k in ["j", "d", "d", "u", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.35)
read_until("]# ", 8.0)
check("dd -> u で行が戻る", run("cat VI9.TXT"), "L1\nL2\nL3")

#   挿入(i..ESC)は 1 段。ESC を押した時点で積む
os.write(master, b"vi VI9.TXT\n")
drain(2.0)
for k in ["i", "XY", "\x1b", "u", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.35)
read_until("]# ", 8.0)
check("i..ESC -> u で挿入ぶんが消える", run("cat VI9.TXT"), "L1\nL2\nL3")

#   p(貼り付け)の取り消し
os.write(master, b"vi VI9.TXT\n")
drain(2.0)
for k in ["y", "y", "p", "u", ":wq\r"]:
    os.write(master, k.encode("latin-1"))
    time.sleep(0.35)
read_until("]# ", 8.0)
check("yy p -> u で貼った行が消える", run("cat VI9.TXT"), "L1\nL2\nL3")

# ---- 差分描画: カーソル移動だけなら 24 行を描き直さない ----
#   全画面を毎打鍵送ると実機のシリアル(115200)で 1 キー ~170ms かかる。
#   移動キーはステータス行 + カーソル移動だけ、編集キーは全面、を出力量で見る。
run("rm VI4.TXT")
run("echo aaa > VI4.TXT")
os.write(master, b"vi VI4.TXT\n")
drain(2.0)
os.write(master, b"l")                   # 右へ 1 つ = 本文は変わらない
move_bytes = len(drain(1.0))
os.write(master, b"x")                   # 行内の削除 = その行だけ描き直す
edit_bytes = len(drain(1.0))
os.write(master, b"o")                   # 行が増える = 24 行を描き直す
full_bytes = len(drain(1.0))
os.write(master, b"\x1b:q!\r")
read_until("]# ", 8.0)
ok_diff = (move_bytes < 200) and (edit_bytes < 200) and (full_bytes > edit_bytes * 2)
if not ok_diff:
    fails += 1
print(f"[{'OK ' if ok_diff else 'FAIL'}] 差分描画: 移動 {move_bytes}B / 行内編集 {edit_bytes}B "
      f"/ 全面 {full_bytes}B (行内編集は 200B 未満で全面の半分未満)")

# ---- ブロック不足(vi は 3 連続ブロックを要求する。#69 で 4 → 3)----
#   sh が 3 ブロック、プロセス枠は block2..7 の 6 個。背景ジョブが 1 個でも
#   居ると sh(3) + sleep(1) + vi(3) = 7 で取れない。クラッシュせず sh が
#   報告することを見る。z80board では sleep が即座に終わってこれが通って
#   いた(KYIELD が tick を進めていた。#71)。
#   最後に置いてあるのは、失敗しても後続テストを巻き込まないため。
run("echo x > VI4.TXT")
run("sleep 6 &")
got = run("vi VI4.TXT", 8.0)
ok_nofree = "no free block" in got
if not ok_nofree:
    fails += 1
print(f"[{'OK ' if ok_nofree else 'FAIL'}] ブロック不足は no free block で断る -> {got!r}")
time.sleep(7)                       # 背景 sleep の終了を待つ
run("")

# ---- 後始末 ----
for f in ("VI.TXT", "VI2.TXT", "VI3.TXT", "VI4.TXT", "VI5.TXT", "VI6.TXT", "VI7.TXT",
          "VI8.TXT", "VI9.TXT",
          "VIB.TXT", "VIC.TXT", "VID.TXT", "VIE.TXT"):
    run("rm " + f)

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
