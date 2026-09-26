#!/usr/bin/env python3
# test_vfs_step2.py - VFS 一本化 Step 2: fat_* を vfs_resolve 経由に
#   ・ls /dev        → null(DEVFS 列挙)
#   ・cat /dev/null  → cannot open(Step 8 で cat は外部化。外部コマンドから
#                     /dev 実体アクセスは drv_open が弾く = Step 4 の "当面弾く"。
#                     旧 builtin fat_cat は即 EOF だったが仕様変更)
#   ・ls /           → 従来どおり FAT ルート(dev は Step 5 まで出ない)
#   ・mkdir /dev / rm /dev/null → permission denied(書込み系は /dev 配下を拒否)
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
print("\n=== steps ===")


def check(cmd, pred, desc):
    global fails
    got = run(cmd)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<22} -> {got!r:<40} ({desc})")
    return got


root_ls = run("ls /")
print(f"(ref) ls /  -> {root_ls!r}")

check("ls /dev",       lambda g: g == "null",                    "DEVFS 列挙で null のみ")
check("cat /dev/null", lambda g: "cannot open" in g,            "外部 cat は /dev 実体を開けない(Step 4/8)")
check("ls /",          lambda g: "dev/" in g.split("\n") and "bin/" in [x.lower() for x in g.split("\n")],
      "FAT ルート(bin/ root/)+ dev 合成。ディレクトリは末尾 /")
check("mkdir /dev",    lambda g: "permission denied" in g,      "書込み系は /dev を拒否")
check("rm /dev/null",  lambda g: "permission denied" in g,      "rm /dev/null 拒否")
check("cat /dev",      lambda g: "cannot open" in g,            "外部 cat は /dev を開けない(Step 4/8)")
check("ls /dev/null",  lambda g: "cannot open" in g,            "非ディレクトリの opendir は失敗")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
