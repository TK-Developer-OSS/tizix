#!/usr/bin/env python3
# test_mbox.py - 郵便受け(src/mbox.c、user/mbox.h、#112)と期限つきの眠り(src/phdr.h)
#
#   使い方: python3 python/test_mbox.py [esp32-wroom-32e|m68k-mega](既定 esp32-wroom-32e)
#   スロット方式(PLAT_FLAT32)のアーキ用。試験用コマンドは user/mbtest.c。
#     ・宛先の無い依頼は -1(no mailbox)
#     ・背景の受け手(mbtest s up &)へ依頼 → 大文字の返事。2 回続けて
#     ・同じ名前はもう開けない
#     ・mb_recv の時間切れ(0.5 秒)と ksleep(0.5 秒)が、だいたいその長さで戻る
#     ・眠っているプロセスがいても依頼は通る
#     ・受け手が終わったら、その名前への依頼は -1 に戻る
import os
import re
import sys
import pty
import time
import select
import subprocess

os.environ["TIZIX_ARCH"] = sys.argv[1] if len(sys.argv) > 1 else "esp32-wroom-32e"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

master, slave = pty.openpty()
proc = subprocess.Popen(
    tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
    stdin=slave, stdout=slave, stderr=slave, close_fds=True,
)
os.close(slave)


def read_until(pat, timeout=8.0):
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


def run(cmd, timeout=8.0):
    os.write(master, (cmd + "\r").encode())
    out = read_until("]# ", timeout).replace("\r", "")
    print("$ " + cmd + "\n" + out)
    return out


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + repr(detail)))
    if not cond:
        fails += 1


def ticks(out, word):
    m = re.search(word + r" (\d+)", out)
    return int(m.group(1)) if m else -1


boot = read_until("]# ", 30.0)
check("boot prompt", boot.endswith("]# "), boot[-80:])

out = run("mbtest c up hi")
check("宛先が無ければ -1", "no mailbox up" in out, out)

run("mbtest s up &")
time.sleep(1.0)
out = run("mbtest c up hello")
check("依頼 → 大文字の返事", "reply: HELLO" in out, out)
out = run("mbtest c up tizix")
check("2 回目の依頼", "reply: TIZIX" in out, out)

out = run("mbtest s up")
check("同じ名前は開けない", "cannot bind up" in out, out)

out = run("mbtest t tm 50")
t = ticks(out, "timeout")
check("mb_recv の時間切れ(0.5 秒)", 40 <= t <= 100, out)

out = run("mbtest z 50")
t = ticks(out, "slept")
check("ksleep(0.5 秒)", 45 <= t <= 100, out)

run("mbtest z 300 &")
time.sleep(0.5)
out = run("mbtest c up abc")
check("眠っているプロセスがいても依頼が通る", "reply: ABC" in out, out)

out = run("mbtest c up quit")
check("受け手を終わらせる", "reply: bye" in out, out)
time.sleep(0.5)
out = run("mbtest c up again")
check("受け手が居なくなったら -1", "no mailbox up" in out, out)

time.sleep(3.0)          # z 300 & の終わりを待つ
out = run("ps")
check("後に残るプロセスが無い", "mbtest" not in out, out)

proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
