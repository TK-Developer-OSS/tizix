#!/usr/bin/env python3
# test_vfs_step9.py - 外部 mkdir / rm / mv(VFS 一本化 Step 9)
#   ・mkdir DIR            : drv_tbl[24] kfs_mkdir
#   ・rm FILE / rm DIR(空) : drv_tbl[25] kfs_unlink
#   ・mv SRC DST           : drv_tbl[26] kfs_rename
#   ・/dev 配下の書込みは permission denied(vfs_resolve が弾く)
#   ・存在しない対象は FatFs FRESULT をそのまま error N で表示
#   ・cwd!="/" のとき resolve_arg で絶対化されて渡る
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


def run(cmd, timeout=5.0):
    os.write(master, (cmd + "\n").encode())
    out = read_until("]# ", timeout)
    lines = out.replace("\r", "").split("\n")
    if lines and lines[-1].endswith("# "):
        lines = lines[:-1]            # プロンプト行 "[cwd]# " を落とす
    body = "\n".join(lines[1:])
    return body.strip("\n")


boot = read_until("System Driver .. LOADED\r\n[/root]# ", 12.0)
read_until("\x00" * 99, 0.6)
print("=== boot ===\n" + boot)

fails = 0


def check(cmd, pred, desc, tmo=5.0):
    global fails
    got = run(cmd, tmo)
    ok = pred(got)
    if not ok:
        fails += 1
    print(f"[{'OK ' if ok else 'FAIL'}] $ {cmd:<26} -> {got!r:<40} ({desc})")
    return got


# ---- mkdir ----
check("mkdir S9", lambda g: g == "", "作成成功(無言)")
check("ls", lambda g: "S9/" in g.split("\n"), "カレント一覧に S9(ディレクトリ = 末尾 /)")
check("mkdir S9", lambda g: "error" in g, "既存 → FatFs エラー(FR_EXIST)")

# ---- ディレクトリ配下のファイル(mkdir が実際に効いている確認)----
run("echo hi step9 > S9/F.TXT")
check("cat S9/F.TXT", lambda g: g == "hi step9", "S9 配下にファイルを置ける")

# ---- mv ----
check("mv S9/F.TXT S9/G.TXT", lambda g: g == "", "リネーム成功(無言)")
check("cat S9/G.TXT", lambda g: g == "hi step9", "新名で読める")
check("cat S9/F.TXT", lambda g: "cannot open" in g, "旧名は消えている")

# ---- rm ----
check("rm S9/G.TXT", lambda g: g == "", "ファイル削除(無言)")
check("rm S9", lambda g: g == "", "空ディレクトリ削除(無言)")
check("ls", lambda g: "S9/" not in g.split("\n"), "S9 は消えた")

# ---- /dev 配下は拒否 ----
check("mkdir /dev/X", lambda g: "permission denied" in g, "mkdir /dev/X 拒否")
check("rm /dev/null", lambda g: "permission denied" in g, "rm /dev/null 拒否")
check("mv /dev/null Y", lambda g: "permission denied" in g, "mv /dev/null 拒否")

# ---- 存在しない対象 ----
check("rm /nope", lambda g: "error" in g, "rm 存在しない → error N")
check("mv /nope /nope2", lambda g: "error" in g, "mv 存在しない → error N")

# ---- cwd!="/" の resolve_arg ----
run("mkdir D1")
run("cd D1")
check("mkdir SUB", lambda g: g == "", "サブディレクトリ内で mkdir")
run("echo z > FF")
check("mv FF FF2", lambda g: g == "", "相対 mv")
check("cat FF2", lambda g: g == "z", "相対 mv 後に読める")
check("rm FF2", lambda g: g == "", "相対 rm")
check("rm SUB", lambda g: g == "", "相対 rmdir")
run("cd /root")
check("rm D1", lambda g: g == "", "ホームから D1 削除")
check("ls", lambda g: "D1/" not in g.split("\n"), "D1 も消えた")

os.write(master, b"exit\n")
time.sleep(0.3)
proc.terminate()
print(f"\n=== result: {'PASS' if fails == 0 else f'{fails} FAIL'} ===")
sys.exit(1 if fails else 0)
