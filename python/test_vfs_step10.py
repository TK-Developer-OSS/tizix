#!/usr/bin/env python3
# test_vfs_step10.py - cp のディレクトリ宛て対応 + 外部 touch(VFS 一本化 Step 10)
#   ・cp SRC DIR      : DIR/basename(SRC) へコピー
#   ・cp SRC DIR/NAME : そのファイル名へ(従来どおり)
#   ・cp SRC .        : カレント(cwd!="/" は sh が絶対化、"/" は cp が / と解釈)
#   ・touch NEW       : 空ファイル作成 / 既存は非破壊 / /dev は cannot create
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


def run(cmd, timeout=5.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]            # プロンプト行 "[cwd]# " を落とす
    body = "\n".join(lines[1:])
    return body.strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
read_until("\x00" * 99, 0.6)
print("=== boot ===\n" + boot)

fails = 0


def check(cmd, pred, desc, tmo=5.0):
    global fails
    got = run(cmd, tmo)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<28} -> {got!r:<40} ({desc})")
    return got


# ---- 準備 ----
run("echo content123 > SRC.TXT")
run("mkdir CPD")

# ---- cp SRC DIR(cwd=/root)----
check("cp SRC.TXT CPD", lambda g: "CPD/SRC.TXT" in g and "bytes" in g,
      "ディレクトリ宛て = DIR/basename")
check("cat CPD/SRC.TXT", lambda g: g == "content123", "中身が一致")

# ---- cp SRC DIR/NAME(従来どおり)----
check("cp SRC.TXT CPD/REN.TXT", lambda g: "REN.TXT" in g, "明示ファイル名へ")
check("cat CPD/REN.TXT", lambda g: g == "content123", "中身が一致")

# ---- cp SRC DST(素のファイルコピー・回帰)----
check("cp SRC.TXT DST.TXT", lambda g: "DST.TXT (11 bytes)" in g, "素のファイルコピー(content123 + LF)")
check("cat DST.TXT", lambda g: g == "content123", "回帰: 中身一致")

# ---- cp SRC . (カレントへ)----
#   #28: sh は引数を絶対化しなくなった(cwd はカーネルが持ち、パスを受け取る
#   カーネル入口が解決する)。よって cp は渡された相対パスをそのまま表示する
#   ── 表示は "./DST.TXT" になる。ここは表示文字列ではなく「カレントに実際に
#   置かれたか」を見るべきなので、次行の cat が本命の検証。
run("cd CPD")
check("cp /root/DST.TXT .", lambda g: "DST.TXT" in g and "bytes" in g,
      "cp SRC . が実行できる")
check("cat DST.TXT", lambda g: g == "content123", "カレントに置かれた")
run("cd /root")

# ---- touch ----
check("touch NEW.TXT", lambda g: g == "", "空ファイル作成(無言)")
check("cat NEW.TXT", lambda g: g == "", "中身は空")
check("ls", lambda g: "NEW.TXT" in g.split("\n"), "カレント一覧に出る")
run("echo keepme > KEEP.TXT")
check("touch KEEP.TXT", lambda g: g == "", "既存 touch は無言")
check("cat KEEP.TXT", lambda g: g == "keepme", "既存は切り詰めない(非破壊)")
# #32 で /dev の実体が繋がり、/dev/null は **開ける**ようになった。よって
#   `touch /dev/null` は本家 Unix と同じく無言で成功する。ここで守りたい
#   不変条件は「/dev の下に新しいノードを生やせない」ことなので、存在しない
#   名前の方で確かめる。
check("touch /dev/null", lambda g: g == "", "/dev/null は実体があるので touch できる")
check("touch /dev/nope", lambda g: "cannot create" in g, "/dev 配下に新規ノードは作れない")

# ---- cwd!="/" の touch ----
run("cd CPD")
check("touch T2.TXT", lambda g: g == "", "サブディレクトリ内で touch")
check("cat T2.TXT", lambda g: g == "", "空で作られた")
run("cd /root")
check("cat CPD/T2.TXT", lambda g: g == "", "ホームから見ても空ファイル")

# ---- エラー ----
check("cp NOPE X.TXT", lambda g: "cannot open" in g, "存在しない src")

# ---- cleanup ----
run("rm CPD/SRC.TXT"); run("rm CPD/REN.TXT"); run("rm CPD/DST.TXT"); run("rm CPD/T2.TXT")
run("rm CPD"); run("rm SRC.TXT"); run("rm DST.TXT"); run("rm NEW.TXT"); run("rm KEEP.TXT")
check("ls", lambda g: "CPD/" not in g.split("\n") and "SRC.TXT" not in g.split("\n"),
      "後始末完了")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
