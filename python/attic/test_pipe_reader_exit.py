#!/usr/bin/env python3
# test_pipe_reader_exit.py - パイプの reader が先に終了しても writer がハングしない
#   ・a | echo  : echo は stdin を読まず即終了 → writer(a=無限ループ)を刈る
#   ・ls /bin | echo : ls(有限)も同様に打ち切り
#   ・回帰: ls /bin | prx(正しく全部流れる)。直前の kill で kdir 状態が
#     漏れて "cannot open" にならないこと(kdir_open の自己回収)。
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
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout=5.0):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            c = os.read(master, 4096)
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                break
    return buf.decode("latin-1", "replace")


def run(cmd, timeout=5.0):
    os.write(master, (cmd + "\n").encode())
    return read_until("]# ", timeout)


read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
read_until("\x00" * 99, 0.5)
fails = 0


def check(cmd, pred, desc, tmo=5.0):
    global fails
    t0 = time.time()
    out = run(cmd, tmo)
    dt = time.time() - t0
    got_prompt = out.rstrip().endswith("#")
    ok = got_prompt and pred(out)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<22} {dt:4.1f}s  ({desc})")
    if not ok:
        print(f"       out={out!r}")
    return out


# reader(echo)が即終了 → writer を刈ってプロンプトが戻る(ハングしない)
check("a|echo", lambda o: True, "無限 writer | echo → 自動終了", tmo=4.0)
check("pwd", lambda o: "/root" in o, "後続コマンドが生きている")
check("ls /bin|echo", lambda o: True, "有限 writer | echo → 終了", tmo=4.0)
# 直前の kill 後も ls パイプが正常(kdir 自己回収)
check("ls /bin|prx", lambda o: "rx: TOUCH.BIN" in o and "prx: EOF" in o,
      "ls|prx は全エントリ流れる(kdir リーク無し)", tmo=6.0)
check("cat /bin/HELLO.BIN|prx", lambda o: "Hello from C main!" in o and "prx: EOF" in o,
      "cat|prx でファイル内容が流れる", tmo=6.0)
check("pwd", lambda o: "/root" in o, "最後までシェル健在")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
