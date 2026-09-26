#!/usr/bin/env python3
# test_pwd_cd.py - pwd / cd 復活の cpmsim 統合テスト
#
#   シェル側 cwd 文字列 + sh 集中解決(相対パス絶対化)を検証する。
#   mkdir/cd/pwd/ls/cat/rm と、リダイレクトファイル名の cwd 解決を通す。
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


def run(cmd, timeout=4.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    # コマンドのエコー行と末尾プロンプトを落として中身だけ返す
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]            # プロンプト行 "[cwd]# " を落とす
    body = "\n".join(lines[1:])
    return body.strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 6.0)
print("=== boot ===")
print(boot)

steps = [
    ("cd /",                     ""),     # 初期カレントは /root。この試験はルート起点で回す
    ("pwd",                      "/"),
    ("mkdir AAA",                ""),
    ("mkdir AAA/BBB",            ""),
    ("cd AAA",                   ""),
    ("pwd",                      "/AAA"),
    ("ls",                       "BBB/"),   # BBB はディレクトリ → 末尾 /
    ("cd BBB",                   ""),
    ("pwd",                      "/AAA/BBB"),
    ("cd ../..",                 ""),
    ("pwd",                      "/"),
    ("cd AAA/BBB",               ""),
    ("pwd",                      "/AAA/BBB"),
    ("echo hello>F.TXT",         ""),   # '>' 前後にスペースを置かない(echo が空白も引数に取る既存仕様)
    ("cat F.TXT",                "hello"),
    ("ls",                       "F.TXT"),
    ("cd /",                     ""),
    ("cat AAA/BBB/F.TXT",        "hello"),
    ("rm AAA/BBB/F.TXT",         ""),
    ("cd AAA/BBB",               ""),
    ("ls",                       ""),
    ("cd ..",                    ""),
    ("rm BBB",                   ""),
    ("ls",                       ""),
    ("cd /",                     ""),
    ("rm AAA",                   ""),
    ("cd NOPE",                  "cd: NOPE: no such dir"),   # #27: sh ダイエットで文言短縮
    ("pwd",                      "/"),
]

fails = 0
print("\n=== steps ===")
for cmd, want in steps:
    got = run(cmd)
    ok = (got == want)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd!s:<24}  want={want!r:<32} got={got!r}")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()

print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
