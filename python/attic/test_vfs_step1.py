#!/usr/bin/env python3
# test_vfs_step1.py - VFS 一本化 Step 1 の回帰確認
#   ・vtree 縮小(/ + /dev + /dev/null のみ)を tree で目視
#   ・> /dev/null が従来どおり出力を捨てる
#   ・vfs_resolve は追加しただけ(まだ誰も呼ばない)ので挙動不変
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
print("=== boot ===")
print(boot)

fails = 0
print("\n=== steps ===")

tree = run("tree")
print("tree:\n" + tree)
# 期待: 3 ノード。/  /dev  /dev/null。旧 /proc /bin /root /var は出ないこと。
for good in ["/dev", "null"]:
    if good not in tree:
        print(f"  [FAIL] tree に {good!r} が無い"); fails += 1
for bad in ["proc", "bin", "root", "/var"]:
    if bad in tree:
        print(f"  [FAIL] tree に削除済みの {bad!r} が残っている"); fails += 1
if fails == 0:
    print("  [OK ] tree = 縮小ツリー(/ + /dev + /dev/null)")

r = run("echo vis")
print(f"echo vis            -> {r!r}")
if r != "vis":
    print("  [FAIL] echo 制御が壊れている"); fails += 1

r = run("echo hid > /dev/null")
print(f"echo hid > /dev/null -> {r!r}")
if r != "":
    print("  [FAIL] > /dev/null が出力を捨てていない"); fails += 1
else:
    print("  [OK ] > /dev/null は従来どおり出力を捨てる")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()

print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
