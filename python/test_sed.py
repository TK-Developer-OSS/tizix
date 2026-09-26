#!/usr/bin/env python3
# test_sed.py - 外部コマンド sed (#34)
#   sed s/OLD/NEW/[g] [file] : 各行を置換して出力。g なしは行内 1 回だけ。
#   ファイル引数なしは標準入力(パイプ後段 / < FILE)。
#
# 注意: 引数なし sed は stdin を読んで戻らない(実機の sed と同じ)。
#   ハーネスから「ファイル無し・入力無し」を直接叩くとシェルごと固まるので、
#   stdin 経路は必ずパイプかリダイレクトで与えること。
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
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<36} -> {got!r:<42} ({desc})")
    return got


# ---- 準備 ----
for f in ("SED.TXT", "SED2.TXT", "SEDOUT.TXT"):
    run("rm " + f)
run("echo hello world > SED.TXT")
run("echo foo bar foo > SED2.TXT")

# ---- ファイル入力 ----
check("sed s/hello/HI/ SED.TXT",  lambda g: g == "HI world",    "先頭一致の置換")
check("sed s/world/z80/ SED.TXT", lambda g: g == "hello z80",   "行途中の置換")
check("sed s/nomatch/X/ SED.TXT", lambda g: g == "hello world", "不一致は素通し")
check("sed s/o/0/ SED.TXT",       lambda g: g == "hell0 world", "1 文字パターン(最初の 1 個)")

# ---- g フラグ ----
check("sed s/foo/F/ SED2.TXT",  lambda g: g == "F bar foo",   "g なしは行内 1 回だけ")
check("sed s/foo/F/g SED2.TXT", lambda g: g == "F bar F",     "g で行内全部")
check("sed s/o/0/g SED.TXT",    lambda g: g == "hell0 w0rld", "g + 1 文字パターン")

# ---- 置換文字列が空(削除)----
check("sed s/hello//g SED.TXT", lambda g: g == " world", "NEW 空 = 削除(残った空白はそのまま)")

# ---- 標準入力(パイプ)----
check("cat SED.TXT | sed s/world/PIPE/", lambda g: g == "hello PIPE", "パイプ後段")
check("echo aaa | sed s/aaa/bbb/",       lambda g: g == "bbb",        "echo からのパイプ")
check("cat SED2.TXT | sed s/foo/F/g",    lambda g: g == "F bar F",    "パイプ + g")

# ---- リダイレクト出力 ----
run("sed s/hello/OUT/ SED.TXT > SEDOUT.TXT")
check("cat SEDOUT.TXT", lambda g: g == "OUT world", "リダイレクトで保存できる")

# ---- エラー / usage ----
check("sed",                 lambda g: "usage" in g,       "引数なしは usage")
check("sed hello SED.TXT",   lambda g: "usage" in g,       "s/// 形式でない")
check("sed s/abc SED.TXT",   lambda g: "usage" in g,       "区切りが足りない")
check("sed s//X/ SED.TXT",   lambda g: "usage" in g,       "空パターンは拒否")
check("sed s/a/b/ NOPE.TXT", lambda g: "cannot open" in g, "存在しないファイル")

# ---- 後始末 ----
for f in ("SED.TXT", "SED2.TXT", "SEDOUT.TXT"):
    run("rm " + f)

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
