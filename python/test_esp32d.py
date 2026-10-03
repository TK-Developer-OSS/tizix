#!/usr/bin/env python3
# test_esp32d.py - 常駐 esp32d と、郵便受けで依頼する esp32-* コマンド(#112)
#
#   使い方: python3 python/test_esp32d.py [esp32-wroom-32e]
#   QEMU でも実機(TIZIX_HW)でも回る。ピンの値そのものは QEMU が周辺機器を真似ないので
#   実機のときだけ見る(TIZIX_HW が立っているとき)。
#     ・/etc/rc が esp32d を起こしていて、esp32-* の依頼に返事(拒否の文言など)が届く
#     ・二重には起きない
#     ・esp32d を kill すると esp32-* は「動いていない」と言う(勝手には起こさない)。手で起こせば戻る
#     ・(実機)GPIO の書いて読む、PWM の一覧、WS2812(esp32-rgb)
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

HW = bool(os.environ.get("TIZIX_HW"))

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


def run(cmd, timeout=10.0):
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


def esp32d_slot(ps):
    for l in ps.split("\n"):
        m = re.match(r"\s*(\d+)\s+\S+\s+esp32d", l)
        if m:
            return int(m.group(1))
    return -1


boot = read_until("]# ", 30.0)
check("boot prompt", boot.endswith("]# "), boot[-80:])

out = run("ps")
n = esp32d_slot(out)
check("/etc/rc が esp32d を起こしている", n > 0, out)

out = run("esp32-gpio 6 1")
check("依頼 → 返事(拒否の文言)", "SPI flash (refused)" in out, out)

out = run("esp32-adc 5")
check("2 回目の依頼(別の装置)", "PIN must be 32..39" in out, out)
out = run("esp32-i2c")
check("使い方の返事", "usage: esp32-i2c" in out, out)

run("esp32d")
out = run("ps")
check("二重には起きない", out.count("esp32d") == 1, out)

run("kill %d" % n)
time.sleep(0.5)
out = run("ps")
check("kill で esp32d が消える", esp32d_slot(out) < 0, out)
out = run("esp32-gpio 1 0")
check("居なければ「動いていない」(勝手には起こさない)", "esp32d is not running" in out, out)
run("esp32d &")
time.sleep(1.0)
out = run("esp32-gpio 1 0")
check("手で起こせばまた通る", "console UART (refused)" in out, out)
out = run("esp32-rgb pink")
check("esp32-rgb の使い方", "usage: esp32-rgb" in out, out)
out = run("esp32-rgb 1 2 3 6")
check("esp32-rgb もピンを守る", "SPI flash (refused)" in out, out)

if HW:
    run("esp32-gpio 5 1")
    out = run("esp32-gpio 5")
    check("(実機)GPIO5 に 1 を書いて読む", re.search(r"^1$", out, re.M) is not None, out)
    run("esp32-gpio 5 0")
    out = run("esp32-gpio 5")
    check("(実機)GPIO5 に 0 を書いて読む", re.search(r"^0$", out, re.M) is not None, out)
    out = run("esp32-pwm 5 1000 50")
    check("(実機)PWM を出す", "GPIO5 ch" in out, out)
    out = run("esp32-pwm")
    check("(実機)PWM の一覧に出る(esp32-pwm が終わっても続く)", re.search(r"^\d\s+5\s", out, re.M) is not None, out)
    run("esp32-pwm 5 off")
    for c in ("red", "green", "blue", "off"):
        out = run("esp32-rgb " + c)
        check("(実機)esp32-rgb %s が通る" % c, "esp32-rgb:" not in out, out)

proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
