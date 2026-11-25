#!/usr/bin/env python3
"""test_z80_zxspectrum.py - end-to-end test of arch/z80-zxspectrum/plus3 on ZEsarUX (ZX Spectrum +3).
  make ARCH=z80-zxspectrum builds disks/disk3.dsk; ZEsarUX (no window: --vo null) boots it from the +3 menu (Loader), the keys are
  sent over its remote protocol (ZRCP) and the screen is read as text from the console's shadow buffer (32x24 bytes at 0xF400).
  ZESARUX = the binary (default /mnt/sda1/zx/ZEsarUX-13.0/zesarux), ZX_LIBS = a directory with libSDL-1.2.so.0 if the
  system has none.  Usage: python3 python/test_z80_zxspectrum.py"""
import os, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZESARUX = os.environ.get("ZESARUX", "/mnt/sda1/zx/ZEsarUX-13.0/zesarux")
LIBS = os.environ.get("ZX_LIBS", "/mnt/sda1/zx/lib/usr/lib/x86_64-linux-gnu")
PORT = int(os.environ.get("ZX_PORT", "10017"))
HOLD = int(os.environ.get("ZX_HOLD", "300"))      # ms each key is held by send-keys-string
DSK = os.path.join(ROOT, "arch/z80-zxspectrum/plus3/disks/disk3.dsk")
subprocess.check_call(["make", "-s", "ARCH=z80-zxspectrum", "DEVICE=plus3"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
env = dict(os.environ, LD_LIBRARY_PATH=LIBS + ":" + os.environ.get("LD_LIBRARY_PATH", ""))
p = subprocess.Popen([ZESARUX, "--noconfigfile", "--vo", "null", "--ao", "null", "--machine", "P341", "--enable-dsk", "--dsk-file", DSK,
                      "--enable-remoteprotocol", "--remoteprotocol-port", str(PORT), "--quickexit"],
                     env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
sk = None
for _ in range(40):
    try:
        sk = socket.create_connection(("127.0.0.1", PORT)); break
    except OSError:
        time.sleep(0.5)
if not sk:
    p.kill(); sys.exit("cannot connect to ZEsarUX")
time.sleep(0.5); sk.recv(4096)

def cmd(c, w=0.3):
    sk.send((c + "\n").encode()); time.sleep(w)
    return sk.recv(65536).decode(errors="replace")

def screen():
    r = cmd("read-memory 62464 768", 0.8)
    h = "".join(l.strip() for l in r.splitlines() if l.strip() and all(c in "0123456789ABCDEFabcdef" for c in l.strip()))
    b = bytes.fromhex(h)[:768]
    return "\n".join(b[i:i + 32].decode("latin1").rstrip() for i in range(0, 768, 32)).rstrip()

def typ(line):
    """type a line and Enter.  | is Symbol Shift + S (ZEsarUX types it through the Extended mode, which the +3 ROM keyboard has)."""
    for i, part in enumerate(line.split("|")):
        if i:
            cmd("set-ui-io-ports FFFDFFFFFFFFFFFD00", 0.3); time.sleep(0.4)
            cmd("set-ui-io-ports FFFFFFFFFFFFFFFF00", 0.3); time.sleep(0.4)
        if part:
            cmd("send-keys-string %d " % HOLD + part, 0.1); time.sleep(HOLD / 1000 * len(part) + 0.6)
    cmd("send-keys-ascii 300 13", 0.1); time.sleep(0.8)

def wait_prompt(t):
    end = time.time() + t
    while time.time() < end:
        sc = screen()
        if sc.endswith("]#"): return sc
        time.sleep(0.5)
    return sc

def run(line, t=30):
    typ(line)
    return wait_prompt(t)

time.sleep(3)
cmd("send-keys-ascii 300 13", 0.1)               # the +3 menu: Loader
sc = wait_prompt(60)
print("boot screen tail:", repr(sc[-80:]))
tests = [
    ("echo abc | cat",        "abc"),
    ("ls /bin | wc",          None),
    ("echo hi > /root/t.txt", None),
    ("cat /root/t.txt",       "hi"),
    ("cat /etc/rc",           "rsyslog"),
    ("uptime",                "up "),
]
fail = 0
for line, want in tests:
    full = run(line)
    out = "\n".join(l for l in full.splitlines() if line not in l)       # without the typed line itself
    if os.environ.get("ZX_V"): print(full)
    ok = (want is None and "EXC" not in out) or (want is not None and want in out and "EXC" not in out)
    print("%s  %s" % ("PASS" if ok else "FAIL", line))
    if not ok:
        fail += 1
        print(out)
cmd("quit", 0.1)
p.kill()
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
