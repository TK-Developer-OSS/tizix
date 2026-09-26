#!/usr/bin/env python3
# test_pipe_kill.py - park 中のパイプ reader を Ctrl+C で殺した後もブロックが使えるか(#72)
#
#   `sleep 3 | prx` の prx は空パイプで proc_block(blocked[n]=1)して park する。
#   ここで Ctrl+C すると krun_pipe の kill_proc が pid_tbl を 0 にするだけなので、
#   以前は blocked[n]=1 が残り、次に同じブロックへ載った単体コマンド(誰も
#   proc_wake しない)が sched_pick に永久に飛ばされて止まった。次もパイプの
#   reader なら writer の最初の proc_wake が偶然落とすので見えにくい。
#   kexec がブロックを渡す前に blocked を落とす修正の回帰テスト。
#   TIZIX_ARCH=z80board でも回る(tzpaths)。
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
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout):
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


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + detail))
    if not cond:
        fails += 1


boot = read_until("]# ", 30.0)
check("boot", boot.endswith("]# "), repr(boot[-60:]))

for rnd in range(3):
    os.write(master, b"sleep 3 | prx\r")
    time.sleep(1.0)                        # prx が空パイプで park するまで待つ
    os.write(master, b"\x03")              # Ctrl+C → krun_pipe が両方 kill
    out = read_until("]# ", 10.0)
    check("round%d: Ctrl+C でプロンプトへ戻る" % rnd, out.endswith("]# "), repr(out[-80:]))

    # 単体コマンドは空きの最小ブロック = さっきの prx のブロックに載る。
    # blocked が残っていると誰も起こさないので永久に走らない(修正前はここで止まる)。
    os.write(master, ("echo alive-%d\r" % rnd).encode())
    out = read_until("]# ", 8.0)
    # 入力行のエコー "echo alive-N" にも alive-N が含まれるので、行頭一致で見る
    lines = out.replace("\r", "").split("\n")
    check("round%d: Ctrl+C 直後の単体コマンドが走る" % rnd,
          ("alive-%d" % rnd) in lines and out.endswith("]# "), repr(out[-80:]))
    if not out.endswith("]# "):
        break

    os.write(master, b"ptx 3 | prx\r")
    out = read_until("prx: EOF", 10.0)
    check("round%d: 次の ptx 3 | prx が完走" % rnd,
          "rx: line-2" in out and "prx: EOF" in out, repr(out[-120:]))
    read_until("]# ", 5.0)

    os.write(master, b"ps\r")
    out = read_until("]# ", 5.0)
    print(out.replace("\r", ""))

proc.kill()
print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
