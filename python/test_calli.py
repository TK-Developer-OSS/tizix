#!/usr/bin/env python3
# test_calli.py - tzcc の CALLI / FNADDR(#70)を tizix 上で実行して確かめる
#   user/calli.c(tzcc --tizix-user)の出力を期待値と突き合わせる。
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


read_until("]# ", 30.0)
os.write(master, b"calli\r")
out = read_until("calli: done", 15.0)
read_until("]# ", 5.0)
proc.kill()

lines = [l for l in out.replace("\r", "").split("\n") if l]
print("\n".join(lines))
want = ["calli0 7", "calli1 42", "calli3 123", "nest 12", "tbl 10", "tbl 25", "tbl 7", "calli: done"]
got = [l for l in lines if l.split(" ")[0] in ("calli0", "calli1", "calli3", "nest", "tbl", "calli:")]
ok = got == want
print("[%s] CALLI / FNADDR: want %r got %r" % ("PASS" if ok else "FAIL", want, got))
print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
