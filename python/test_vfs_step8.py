#!/usr/bin/env python3
# test_vfs_step8.py - 外部 cat / echo(VFS 一本化 Step 8)
#   ・echo TEXT           : argv[0] + 改行
#   ・echo (無引数)       : 空行
#   ・echo TEXT > FILE    : 外部プロセス + グローバル redir_on でファイルへ
#   ・cat FILE            : fopen/fread
#   ・cat < FILE          : 外部プロセス + グローバル in_on で getchar 経由
#   ・cat /nope           : cannot open
#   ・cat FILE | prx      : 両側外部 = 本物のカーネルパイプ(cat が writer)
#   ・echo X | prx        : echo が writer
#   ・cwd!="/" 時の resolve_arg(cd したサブディレクトリからの相対パス)
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
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<24} -> {got!r:<44} ({desc})")
    return got


# ---- echo ----
check("echo hello world", lambda g: g == "hello world", "argv[0] + 改行")
check("echo", lambda g: g == "", "無引数 = 空行")

# ---- echo > FILE(外部プロセス + redir_on)----
run("echo hello from step8 > T8.TXT")
check("cat T8.TXT", lambda g: g == "hello from step8", "cat FILE (fopen/fread)")

# ---- cat < FILE(外部プロセス + in_on)----
check("cat < T8.TXT", lambda g: g == "hello from step8", "cat < FILE (getchar 経由)")

# ---- エラー ----
check("cat /nope", lambda g: "cannot open" in g, "存在しないファイル")

# ---- カーネルパイプ: cat が writer ----
got = run("cat T8.TXT | prx", 6.0)
print(f"--- cat T8.TXT | prx ---\n{got}")
if "rx: hello from step8" in got and "prx: EOF" in got:
    print("[OK ] cat | prx がカーネルパイプで動く")
else:
    print("[FAIL] cat | prx"); fails += 1

# ---- カーネルパイプ: echo が writer ----
got = run("echo piped text | prx", 6.0)
print(f"--- echo piped text | prx ---\n{got}")
if "rx: piped text" in got and "prx: EOF" in got:
    print("[OK ] echo | prx がカーネルパイプで動く")
else:
    print("[FAIL] echo | prx"); fails += 1

# ---- cwd!="/" の resolve_arg ----
run("mkdir T8D")
run("cd T8D")
run("echo inside subdir > INNER.TXT")
check("cat INNER.TXT", lambda g: g == "inside subdir", "cwd!=/ 相対 cat")
run("cd /root")
check("cat T8D/INNER.TXT", lambda g: g == "inside subdir", "ホームから相対パス cat")

# ---- cleanup ----
run("rm T8D/INNER.TXT")
run("rm T8D")
run("rm T8.TXT")
check("cat T8.TXT", lambda g: "cannot open" in g, "削除後は open 不可(後始末確認)")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
