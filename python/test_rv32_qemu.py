#!/usr/bin/env python3
"""test_rv32_qemu.py - end-to-end test of arch/rv32-qemu (QEMU riscv32 virt).
  make ARCH=rv32-qemu builds kernel.elf and disk.img, boots them on QEMU, feeds commands to the shell and
  and checks the output (loading and relocating external commands, syscalls, preemption, FAT writes).
  Usage: python3 python/test_rv32_qemu.py"""
import os, select, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJ = os.path.join(ROOT, "build/arch/rv32-qemu/obj")
subprocess.check_call(["make", "-s", "ARCH=rv32-qemu"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
cmd = ["qemu-system-riscv32", "-M", "virt", "-bios", "none", "-nographic", "-m", "128M",
       "-kernel", OBJ + "/kernel.elf", "-device", "loader,file=%s/disk.img,addr=0x82000000" % OBJ]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, preexec_fn=os.setsid)
buf = b""

def rd(t):
    global buf
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([p.stdout], [], [], 0.2)
        if r:
            d = os.read(p.stdout.fileno(), 4096)
            if not d:
                return
            buf += d

def run(line, wait=2.5):
    global buf
    buf = b""
    p.stdin.write((line + "\r").encode()); p.stdin.flush()
    rd(wait)
    return buf.decode(errors="replace")

rd(4)
tests = [
    ("hello",                 "Hello from C main!"),
    ("echo abc | cat",        "abc"),
    ("sleep 2",               None),            # checked by the uptime that follows
    ("uptime",                "up "),
    ("echo hi > /root/t.txt", None),
    ("cat /root/t.txt",       "hi"),
    ("ls /bin | wc",          "38"),
    ("tzsh -c 'echo tzsh ok'", "tzsh ok"),
]
fail = 0
for line, want in tests:
    out = run(line, 3.5 if line.startswith("sleep") else 2.5)
    ok = (want is None and "EXC" not in out) or (want is not None and want in out and "EXC" not in out)
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print(out)
os.killpg(p.pid, 9)
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
