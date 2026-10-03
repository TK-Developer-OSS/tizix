#!/usr/bin/env python3
# test_tzsh.py - tzsh(シェルスクリプト専門のシェル、#111)
#
#   使い方: python3 python/test_tzsh.py [esp32-wroom-32e|m68k-mega](既定 esp32-wroom-32e)
#   スクリプトはディスクの /usr/share/tzsh/test1.sh(user/tzsh/test1.sh)。while / for / case / if-elif-else /
#   && || ! / break continue / 外部コマンドの終了コード / > >> < / | / ${} / $(( )) / exit を一通り通す。
#   終了コードは tzsh の中から別の tzsh を起こして $? で見る(sh には $? が無い)。
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


def run(cmd, timeout=20.0):
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

out = run("tzsh /usr/share/tzsh/test1.sh foo")
lines = [l.strip() for l in out.split("\n")]
for want, name in [
    ("i=5", "while と $(( ))"),
    ("abc", "for と echo -n"),
    ("case:fb", "case の型 foo|bar"),
    ("argc1", "if / elif / else と $#"),
    ("and-ok", "&&"),
    ("or-ok", "||"),
    ("not-ok", "!"),
    ("n=3", "break"),
    ("134", "continue"),
    ("hello-rc=0", "外部コマンドの終了コード 0"),
    ("mbtest-rc=1", "外部コマンドの終了コード 1"),
    ("first", "> と cat"),
    ("second", ">>"),
    ("read:first", "read と <"),
    ("piped", "組み込み | 外部"),
    ("tiz ix!", "${名前} と引用符の中の空白"),
    ("$name 3", "'…' は展開しない / $(( )) の優先順位"),
]:
    check(name, want in lines, out)
check("エラーが出ていない", "tzsh:" not in out, out)

out = run("tzsh /usr/share/tzsh/test2.sh")                # 中で別の tzsh を起こす
check("exit 7 → 呼んだ側の $? = 7", "rc=7" in out, out)
check("引数 2 個 → argc2", "argc2" in out, out)
out = run("tzsh /usr/share/tzsh/test3.sh")
check("入れ子のリダイレクトは断る", "nested redirection" in out and "inner" not in out, out)
out = run("tzsh -c 'nosuchcmd; echo rc=$?'")
check("見つからないコマンドは 127", "rc=127" in out and "not found" in out, out)
out = run("tzsh -c 'if true; then echo a'")
check("構文の誤りを言う", "syntax error" in out, out)

proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
