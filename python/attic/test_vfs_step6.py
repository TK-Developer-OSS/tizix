#!/usr/bin/env python3
# test_vfs_step6.py - 外部 ls(opendir/readdir/closedir、drv_tbl[21..23])
#   ・ls / : FAT ルート + 合成 "dev"
#   ・ls /dev : DEVFS(vfs_dir_next)で "null"
#   ・ls SUBDIR : FAT サブディレクトリ
#   ・ls NOPE : cannot open
#   ・ls は builtin 表から抜けたので ls | prx が本物のカーネルパイプに乗る
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


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
# ブート出力を完全に吸い切る(以降の run() が 1 個前の出力を読まないように)
read_until("\x00" * 99, 0.6)
print("=== boot ===\n" + boot)

fails = 0


def check(cmd, pred, desc, tmo=5.0):
    global fails
    got = run(cmd, tmo)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<22} -> {got!r:<50} ({desc})")
    return got


check("ls /", lambda g: "bin/" in [x.lower() for x in g.split("\n")] and "dev/" in g.split("\n"),
      "FAT ルート(bin/ root/)+ dev 合成(末尾 /)")
check("ls /bin", lambda g: "hello.bin" in g.split("\n"), "/bin にコマンドが並ぶ(小文字)")
check("ls /dev", lambda g: g == "null", "DEVFS 反復(null はデバイスなので / 無し)")
check("ls /nope", lambda g: "cannot open" in g, "存在しないパス")

run("mkdir S6DIR")
run("mkdir S6DIR/INNER")
check("ls S6DIR", lambda g: g == "INNER/", "FAT サブディレクトリ(末尾 /)")
run("rm S6DIR/INNER")
run("rm S6DIR")

# ls は builtin 表から抜けた → ls | prx は両側外部 = カーネルパイプ
got = run("ls /dev | prx", 6.0)
print(f"--- ls /dev | prx ---\n{got}")
if "rx: null" in got and "prx: EOF" in got:
    print("[OK ] ls | prx がカーネルパイプで動く")
else:
    print("[FAIL] ls | prx"); fails += 1

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
