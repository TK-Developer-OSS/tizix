#!/usr/bin/env python3
"""test_z80_rc2014.py - end-to-end test of arch/z80-rc2014/romwbw on MAME (rc2014zedp, no window).
  make ARCH=z80-rc2014 builds disks/cf.hd; MAME runs an RC2014 Zed Pro with RomWBW 3.0.1, the card in a Compact Flash
  module and SIO/2 channel A on a null modem whose other end is a TCP socket this script listens on.  The script answers
  RomWBW's boot prompts (disk 2 = IDE0, slice 0), then types each test line as a terminal would and checks the output.
  Needs RomWBW's RCZ80_std.rom (3.0.1) on MAME's rompath as rc2014zedp/rcz80_std_3_0_1.rom (see the README).
  The Dual Clock module's CLOCK2 is set to 0.3072MHz (a MAME config file in a temporary directory): the tick.
  MAME = the binary (default mame).  RC_V=1 prints every screen.  Usage: python3 python/test_z80_rc2014.py"""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAME = os.environ.get("MAME", "mame")
DISK = os.path.join(ROOT, "arch/z80-rc2014/romwbw/disks/cf.hd")
subprocess.check_call(["make", "-s", "ARCH=z80-rc2014"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

CFG = """<?xml version="1.0"?>
<mameconfig version="10">
    <system name="rc2014zedp">
        <input>
            <port tag=":bus:2:dual_clk:CLOCK2" type="CONFIG" mask="15" defvalue="0" value="6" />
        </input>
    </system>
</mameconfig>
"""
# no X display for MAME: with one, MAME (SDL) opens an empty full-screen window even with -video none, on the desktop
NOWIN = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
ESC = "\x1b"
# (what to type, what the output must contain or None); "\x1b" etc. are sent as they are.  The sleep test is last:
# a background job holds a memory block, and vi needs three
tests = [
    ("echo abc | cat",              "abc"),
    ("ls /bin | wc",                None),
    ("echo hi > /root/t.txt",       None),
    ("cat /root/t.txt",             "hi"),
    ("cat /etc/rc",                 "rsyslog"),
    ("uptime",                      "up "),
    ("echo a&b {x} \"q\"",          "a&b {x} q"),
    ("echo X-Y_z:~/@[]^\\;=+*'",    "X-Y_z:~/@[]^\\;=+*"),
    ("echo ?!#$%()|cat",            "?!#$%()"),
    ("vi /root/v.txt",              None),          # then the keys below, without waiting for a prompt
    ("cat /root/v.txt",             "hello"),
    ("sleep 5 &",                   None),
    ("ps",                          "sleep"),
]
VI_KEYS = "ihello" + ESC + ":wq\r"

class Term:
    def __init__(self, conn):
        self.c, self.buf = conn, ""
        self.c.settimeout(0.2)
    def pump(self):
        try:
            d = self.c.recv(4096)
        except socket.timeout:
            return
        if not d: raise EOFError("MAME closed the line")
        self.buf += d.decode("latin-1")
    def wait(self, pat, start, secs):
        end = time.time() + secs
        while time.time() < end:
            i = self.buf.find(pat, start)
            if i >= 0: return i
            self.pump()
        raise TimeoutError("no %r; output ends %r" % (pat, self.buf[-200:]))
    def send(self, s, gap=0.01):
        for ch in s:
            self.c.sendall(ch.encode("latin-1"))
            time.sleep(gap)
            self.pump()

tmp = tempfile.mkdtemp()
mame = None
fail = 0
try:
    shutil.copy(DISK, tmp + "/cf.hd")                   # the disk is written to; keep the built image as it is
    os.mkdir(tmp + "/cfg")
    open(tmp + "/cfg/rc2014zedp.cfg", "w").write(CFG)
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    port = srv.getsockname()[1]
    mame = subprocess.Popen(["nice", "-n", "19", MAME, "rc2014zedp", "-bus:3", "rom_ram,bios=3.0.1", "-bus:5", "cf",
                             "-hard", tmp + "/cf.hd", "-bus:4:sio:rs232a", "null_modem",
                             "-bitb", "socket.127.0.0.1:%d" % port, "-cfg_directory", tmp + "/cfg",
                             "-video", "none", "-sound", "none", "-skip_gameinfo"],
                            cwd=tmp, env=NOWIN, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    srv.settimeout(60)
    conn, _ = srv.accept()
    t = Term(conn)
    t.wait("Boot Selection? ", 0, 120)
    t.send("2")
    t.wait("Slice", 0, 30)
    t.send("\r")
    pos = t.wait("[/root]# ", 0, 120) + 9
    print("boot screen tail:", repr(t.buf[pos - 80:pos]))
    for line, want in tests:
        t.send(line + "\r")
        if line.startswith("vi "):
            time.sleep(8)                               # vi loads its overlays
            t.send(VI_KEYS, 0.05)
        try:
            end = t.wait("[/root]# ", pos, 120)
        except TimeoutError as e:
            print("FAIL  %s  (%s)" % (line, e)); fail += 1; break
        res = t.buf[pos:end]
        pos = end + 9
        if os.environ.get("RC_V"): print(res)
        out = res.split("\n", 1)[1] if "\n" in res else ""
        ok = "EXC" not in out and (want is None or want in out)
        print("%s  %s" % ("PASS" if ok else "FAIL", line))
        if not ok:
            fail += 1
            print(repr(res[-300:]))
except (TimeoutError, EOFError, socket.timeout) as e:
    print("*** %s ***" % e); fail += 1
finally:
    if mame:
        mame.terminate()
        try: mame.wait(10)
        except subprocess.TimeoutExpired: mame.kill()
    shutil.rmtree(tmp)
print("*** ALL PASS ***" if not fail else "*** %d FAIL ***" % fail)
sys.exit(1 if fail else 0)
