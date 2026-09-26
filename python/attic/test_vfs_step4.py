#!/usr/bin/env python3
# test_vfs_step4.py - VFS 一本化 Step 4: DRIVER の FS ベクタを vfs_resolve 経由に
#   ・外部コマンドの FAT ファイル操作は従来どおり(cp A.BIN ZZ.BIN 成功)
#   ・外部コマンドから /dev 配下を開こうとすると弾かれる(cp /dev/null ... 失敗)
#   ・回帰: hello / a(前景) が動く
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


def run(cmd, timeout=5.0):
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
print("\n=== steps ===")


def check(cmd, pred, desc):
    global fails
    got = run(cmd)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<20} -> {got!r:<46} ({desc})")
    return got


check("cp /bin/A.BIN ZZ.BIN", lambda g: "->" in g and "bytes" in g, "外部cp: FAT コピー成功(/bin→/root)")
check("ls",                lambda g: "ZZ.BIN" in g.split("\n"),    "コピー先がルート一覧に出る")
check("cp /dev/null ZZ2",  lambda g: "cannot open" in g,           "外部cp: /dev 配下は弾かれる")
check("ls",                lambda g: "ZZ2" not in g.split("\n"),   "弾かれたので ZZ2 は作られない")
check("rm ZZ.BIN",         lambda g: g == "",                     "後始末")
check("ls",                lambda g: "ZZ.BIN" not in g.split("\n"), "後始末できた")
check("hello",             lambda g: len(g) > 0 and "not found" not in g, "回帰: hello 実行")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
