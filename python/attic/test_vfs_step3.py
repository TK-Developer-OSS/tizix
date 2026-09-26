#!/usr/bin/env python3
# test_vfs_step3.py - VFS 一本化 Step 3: do_cd を vfs_resolve 経由に
#   ・cd /dev        → OK、pwd=/dev、ls で null 列挙
#   ・cd /dev/null   → "not a directory"(ファイルノードには入れない)
#   ・cd /dev/nope   → "no such directory"
#   ・cd .. で / へ戻れる
#   ・FAT サブディレクトリの cd は従来どおり
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
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]            # プロンプト行 "[cwd]# " を落とす
    body = "\n".join(lines[1:])
    return body.strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 6.0)
print("=== boot ===\n" + boot)

fails = 0
steps = [
    ("pwd",           "/root"),           # 初期カレントはホーム
    ("cd /dev",       ""),
    ("pwd",           "/dev"),
    ("ls",            "null"),
    ("cd /dev/null",  "cd: /dev/null: not a directory"),
    ("pwd",           "/dev"),                       # 失敗しても cwd 不変
    ("cd /dev/nope",  "cd: /dev/nope: no such directory"),
    ("cd ..",         ""),
    ("pwd",           "/"),
    ("mkdir XDIR",    ""),
    ("cd XDIR",       ""),
    ("pwd",           "/XDIR"),
    ("cd /dev",       ""),                           # FAT サブ → devfs へ直接
    ("pwd",           "/dev"),
    ("cd /",          ""),
    ("rm XDIR",       ""),
]
print("\n=== steps ===")
for cmd, want in steps:
    got = run(cmd)
    ok = got == want
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<16} want={want!r:<38} got={got!r}")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
