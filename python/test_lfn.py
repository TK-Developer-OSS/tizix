#!/usr/bin/env python3
# test_lfn.py - 長いファイル名(#114。gcc 系 = PLAT_FLAT32 のアーキだけ)
#
#   使い方: python3 python/test_lfn.py [esp32-wroom-32e|m68k-mega](既定 esp32-wroom-32e)
#     ・8.3 に収まらない名前で作る / ls に出る / cat で読める / mv で長い名前どうしに / rm で消える
#     ・大文字小文字を区別せずに開ける(ASCII)
#     ・/bin の長い名前のコマンド(esp32 の esp32-gpio)が「-」の読み替え無しで起動する
import os
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


boot = read_until("]# ", 30.0)
check("boot prompt", boot.endswith("]# "), boot[-80:])

run("rm long-file-name.txt")
run("rm another.long.name.text")
run("echo hello-lfn > long-file-name.txt")
out = run("ls")
check("ls に長い名前", "long-file-name.txt" in out, out)
out = run("cat long-file-name.txt")
check("cat で読める", "hello-lfn" in out, out)
out = run("cat LONG-FILE-NAME.TXT")
check("大文字でも開ける", "hello-lfn" in out, out)
run("mv long-file-name.txt another.long.name.text")
out = run("ls -l")
check("mv で長い名前どうし", "another.long.name.text" in out and "long-file-name.txt" not in out, out)
out = run("cat another.long.name.text")
check("mv 後も読める", "hello-lfn" in out, out)
run("rm another.long.name.text")
out = run("ls")
check("rm で消える", "another.long.name.text" not in out, out)

if tzpaths.ARCH == "esp32-wroom-32e":
    out = run("ls /bin")
    check("/bin に esp32-gpio.bin", "esp32-gpio.bin" in out, out)
    out = run("esp32-gpio 6 1")
    check("長い名前のコマンドが起動する", "SPI flash (refused)" in out, out)

proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
