#!/usr/bin/env python3
# test_ls_format.py - ls の表示形式がアーキ間で揃っているか(#76)
#
#   使い方: python3 python/test_ls_format.py [z80pack|z80board|m68k-mega|esp32-wroom-32e]
#           (省略時は TIZIX_ARCH、既定 z80pack。起動コマンドは tzpaths.py が持つ)
#   検査:
#     ・ディレクトリは末尾 /、-l では " <DIR>  " 欄
#     ・ルートの列挙に dev/ が出る ── "/"・".."・cwd=/ の無引数、どの表記でも
#     ・ls /dev が開けて null が出る
import os
import sys
import pty
import time
import select
import subprocess

ARCH = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("TIZIX_ARCH", "z80pack")
os.environ["TIZIX_ARCH"] = ARCH           # tzpaths は import 時にこれを読む
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

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


def run(cmd):
    os.write(master, (cmd + "\r").encode())
    out = read_until("]# ", 20.0).replace("\r", "").split("\n")
    body = out[1:-1]                      # エコー行とプロンプト行を落とす
    print("--- " + cmd)
    print("\n".join(body))
    return body


fails = 0


def check(name, cond):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name)
    if not cond:
        fails += 1


read_until("]# ", 30.0)

root = run("ls /")
check("ls /: 全エントリがディレクトリで末尾 /", root and all(l.endswith("/") for l in root))
check("ls /: dev/ が出る", "dev/" in root)
check("ls /: bin/ が出る", "bin/" in root)

rootl = run("ls -l /")
check("ls -l /: 全行 <DIR> 欄", rootl and all(l.startswith(" <DIR>  ") for l in rootl))
check("ls -l /: <DIR>  dev/", " <DIR>  dev/" in rootl)
check("ls -l /: <DIR>  bin/", " <DIR>  bin/" in rootl)
check("ls -l /: ディレクトリ行は全部 / で終わる", rootl and all(l.endswith("/") for l in rootl))

dev = run("ls /dev")
check("ls /dev: null が出る", "null" in dev)
check("ls /dev: ファイルに / が付かない", "null/" not in dev)

run("cd /etc")
up = run("ls ..")
check("cd /etc; ls ..: dev/ が出る", "dev/" in up)
run("cd /")
here = run("ls")
check("cd /; ls: dev/ が出る", "dev/" in here)
run("cd /root")

etc = run("ls /etc")
check("ls /etc: rc(ファイル)に / が付かない", "rc" in etc)

check("ls FILE はその名前を出す", run("ls /etc/rc") == ["/etc/rc"])
wc = run("wc -c /etc/rc")
size = wc[0].split()[0] if wc else "?"
check("ls -l FILE はサイズと名前", run("ls -l /etc/rc") == ["%s  /etc/rc" % size])

os.write(master, b"\x1d")                 # m68ksim は Ctrl+] で抜ける
time.sleep(0.3)
proc.kill()
print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
