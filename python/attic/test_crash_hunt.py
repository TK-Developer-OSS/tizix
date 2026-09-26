#!/usr/bin/env python3
# test_crash_hunt.py - フレークな Op-code trap を再現し、直前のマーカ(TZDBG)を晒す。
#   pwd_cd 相当のコマンド列を何回も回し、trap/HALT が出た瞬間の生ログ全部を出す。
import os, sys, pty, time, select, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

SEQ = [
    "pwd", "mkdir AAA", "mkdir AAA/BBB", "cd AAA", "pwd", "ls", "cd BBB", "pwd",
    "cd ../..", "pwd", "cd AAA/BBB", "pwd", "echo hello>F.TXT", "cat F.TXT", "ls",
    "cd /", "cat AAA/BBB/F.TXT", "rm AAA/BBB/F.TXT", "cd AAA/BBB", "ls", "cd ..",
    "rm BBB", "ls", "cd /", "rm AAA", "cd NOPE", "pwd",
]

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 12

for rnd in range(ROUNDS):
    tzpaths.wait_disk_ready(tzpaths.DRIVEB)
    master, slave = pty.openpty()
    proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                            stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    log = []

    def rd(timeout=3.0, stop=None):
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
                if stop and stop.encode() in buf:
                    break
        s = buf.decode("latin-1", "replace")
        log.append(s)
        return s

    rd(6.0, stop="System Driver .. LOADED\r\n[/root]# ")
    crashed = False
    for cmd in SEQ:
        os.write(master, (cmd + "\n").encode())
        out = rd(3.0, stop="]# ")
        if "Op-code trap" in out or "HALT Op-Code" in out or "trap at" in out:
            crashed = True
            break
    try:
        proc.terminate()
    except Exception:
        pass
    full = "".join(log)
    if crashed:
        print(f"\n########## ROUND {rnd}: CRASH ##########")
        print(full[-2500:])
        print("########## (last 2500 chars above) ##########")
        sys.exit(2)
    else:
        print(f"round {rnd}: ok")

print(f"\nno crash in {ROUNDS} rounds")
