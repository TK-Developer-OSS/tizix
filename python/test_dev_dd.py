#!/usr/bin/env python3
# test_dev_dd.py - /dev 生ブロックデバイス + dd の bs=/count=/skip= (#32)
#
#   ゲスト内では「何バイト取れたか」までしか見られないので、**中身の一致は
#   ホスト側で確かめる**: 取り出した .IMG を mtools で driveb.dsk から
#   吸い出し、生イメージ(drivea.dsk / driveb.dsk)の該当オフセットと cmp する。
#   その部分は python/run_dev_dd_check.sh 側。ここはゲストの挙動だけを見る。
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


def run(cmd, timeout=10.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]
    return "\n".join(lines[1:]).strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
print("=== boot ===\n" + boot)

fails = 0


def check(cmd, pred, desc, tmo=10.0):
    global fails
    got = run(cmd, tmo)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<44} -> {got!r:<34} ({desc})")
    return got


# ---- 準備 ----
for f in ("VBR.IMG", "S1.IMG", "BOOT.IMG", "A1.IMG", "SMALL.IMG", "PLAIN.TXT", "PLAIN2.TXT"):
    run("rm " + f)

# ---- /dev の見え方 ----
got = run("ls /dev")
ok = ("null" in got) and ("fda" in got) and ("fdb" in got)
if not ok:
    fails += 1
print(f"[{'OK ' if ok else 'FAIL'}] $ ls /dev -> {got!r} (null/fda/fdb が並ぶ)")

# ---- /dev/null が開けて即 EOF ----
check("cat /dev/null", lambda g: g == "", "/dev/null は空(開けるようになった)")

# ---- 生デバイスから 1 セクタ ----
check("dd if=/dev/fdb of=VBR.IMG bs=512 count=1",
      lambda g: "1 blocks copied" in g, "drive B の VBR を 1 セクタ")
check("ls -l", lambda g: any("VBR.IMG" in L and "512" in L for L in g.splitlines()), "VBR.IMG が 512 バイト")

# ---- skip= ----
#   ★sh の行長上限は LINE_MAX=48。出力名を SEC1.IMG にすると行がちょうど
#   48 文字になり、末尾 1 文字が落ちて skip= が空になる(#32 で踏んだ)。
#   名前を短くして避けている。
check("dd if=/dev/fdb of=S1.IMG bs=512 count=1 skip=1",
      lambda g: "1 blocks copied" in g, "skip=1 で 2 セクタ目")
check("ls -l", lambda g: any("S1.IMG" in L and "512" in L for L in g.splitlines()), "S1.IMG が 512 バイト")

# ---- /dev/fda(boot ディスク)----
#   drive A はゲストが書き換えないので、ホスト側のバイト比較が決定的になる。
#   drive B の 2 セクタ目は FAT 本体で、ゲストがファイルを作った時点で変わる
#   ── なので skip= の中身照合は A 側で行う(B 側は長さだけ見る)。
check("dd if=/dev/fda of=BOOT.IMG bs=512 count=1",
      lambda g: "1 blocks copied" in g, "drive A の先頭セクタ")
check("dd if=/dev/fda of=A1.IMG bs=512 count=1 skip=1",
      lambda g: "1 blocks copied" in g, "drive A の 2 セクタ目(skip=1)")
check("ls -l", lambda g: any("BOOT.IMG" in L and "512" in L for L in g.splitlines()), "BOOT.IMG が 512 バイト")

# ---- count= で複数セクタ ----
check("dd if=/dev/fdb of=SMALL.IMG bs=512 count=2",
      lambda g: "2 blocks copied" in g, "count=2 で 2 セクタ")
check("ls -l", lambda g: any("SMALL.IMG" in L and "1024" in L for L in g.splitlines()), "SMALL.IMG が 1024 バイト")

# ---- 生デバイスは 512 の倍数のみ ----
check("dd if=/dev/fdb of=BAD.IMG bs=128 count=1",
      lambda g: "0 blocks copied" in g or "read error" in g,
      "bs が 512 の倍数でなければ転送しない")
run("rm BAD.IMG")

# ---- 通常ファイル同士の回帰(bs 指定あり / なし)----
run("echo hello-dd > PLAIN.TXT")
check("dd if=PLAIN.TXT of=PLAIN2.TXT",
      lambda g: "blocks copied" in g, "通常ファイルのコピー(既定 bs)")
check("cat PLAIN2.TXT", lambda g: g == "hello-dd", "中身が一致")
run("rm PLAIN2.TXT")
check("dd if=PLAIN.TXT of=PLAIN2.TXT bs=4",
      lambda g: "blocks copied" in g, "bs=4 でも通る(通常ファイルは任意)")
check("cat PLAIN2.TXT", lambda g: g == "hello-dd", "中身が一致")

# ---- /dev へ書き込む口が開いていること(内容は次段で確認)----
check("dd if=VBR.IMG of=/dev/null bs=512 count=1",
      lambda g: "1 blocks copied" in g, "/dev/null への書き込みは捨てて成功")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
