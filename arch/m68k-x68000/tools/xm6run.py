#!/usr/bin/env python3
"""xm6run.py - run tizix (arch/m68k-x68000) on XM6G under Wine + Xvfb and check its output.

  usage:
    xm6run.py boot [disk.xdf]          boot XM6G and wait for the shell prompt (leaves it running)
    xm6run.py run "date" "ls /"        boot, type the commands, print each command's output
    xm6run.py tests x68k_tests.txt     boot and run a test file (exit status 0 = all PASS)
    xm6run.py shot out.png             boot and save a screenshot

  The host keyboard, mouse and screen are never touched: XM6G runs on its own Xvfb display
  (X68K_DISPLAY, default :98).  The console text is not read from the screen: the kernel keeps
  every byte it prints in a ring at $0FF000 (console.c, "TZXLOG01"), and this script reads that
  ring from the XM6G process memory (read-only).  XM6 keeps main RAM with every 2 bytes reversed,
  so the ring is found by searching for the reversed magic.

  Needs XM6G + IPLROM.DAT + CGROM.DAT in XM6_DIR (default ~/tools/xm6) and Wine.

  Test file: one line per test, "command<TAB>expected".  Expected: empty = no output,
  "!text" = must not contain text, "~regex" = must match, anything else = must contain it.
  The line "@vitest" runs a vi session (write a file, :wq, cat it back).
"""
import os, re, shutil, subprocess, sys, tempfile, time

DISPLAY = os.environ.get("X68K_DISPLAY", ":98")
XM6 = os.path.expanduser(os.environ.get("XM6_DIR", "~/tools/xm6"))
HERE = os.path.dirname(os.path.abspath(__file__))
DISK = os.path.join(HERE, "..", "..", "..", "build", "arch", "m68k-x68000", "obj", "disk.xdf")
MAGIC = b"TZXLOG01"
RING = 0x0FF000
PROMPT = re.compile(r"\[[^\]\n]*\]# A*$")   # a background `a` may print A after the prompt
ENV = dict(os.environ, DISPLAY=DISPLAY, WINEPREFIX=os.path.expanduser("~/.wine64"), LC_ALL="ja_JP.UTF-8",
           WINEDEBUG="-all")

_proc = None
_base = None
_tmp = None


def log(s):
    print("[xm6run] " + s, flush=True)


def x(*a):
    return subprocess.run(list(a), env=ENV, capture_output=True, text=True)


def swap2(b):
    b = b + bytes(len(b) % 2)
    return b"".join(b[i:i + 2][::-1] for i in range(0, len(b), 2))


def start_xvfb():
    if x("xdpyinfo").returncode == 0:
        return
    subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"], stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)
    for _ in range(20):
        time.sleep(0.5)
        if x("xdpyinfo").returncode == 0:
            return
    raise RuntimeError("Xvfb did not start")


def boot(disk=None, wait=90):
    global _proc, _base
    disk = os.path.abspath(disk or DISK)
    start_xvfb()
    stop()
    # XM6G writes to the image, so always boot a fresh copy: every run starts from the same disk
    global _tmp
    _tmp = tempfile.mkdtemp(prefix="xm6run")
    shutil.copy(disk, os.path.join(_tmp, "disk.xdf"))
    disk = os.path.join(_tmp, "disk.xdf")
    _proc = subprocess.Popen(["nice", "-n", "19", "/usr/lib/wine/wine64", os.path.join(XM6, "xm6g.exe"), disk],
                             env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    _base = None
    t0 = time.time()
    clicked = False
    while time.time() - t0 < wait:
        time.sleep(2)
        if not clicked and time.time() - t0 > 12:
            # the CG-ROM compatibility warning (a non-genuine CGROM.DAT): "continue"
            x("xdotool", "mousemove", "711", "546", "click", "1")
            clicked = True
        t = console()
        if t is not None and PROMPT.search(t):
            return True
    log("boot: no prompt; console = %r" % (console() or "")[-200:])
    return False


def stop():
    global _proc
    if _proc is not None:
        try:
            _proc.terminate()
            _proc.wait(10)
        except Exception:
            _proc.kill()
        _proc = None
    subprocess.run(["wineserver", "-k"], env=ENV, capture_output=True)
    time.sleep(1)
    global _tmp
    if _tmp:
        shutil.rmtree(_tmp, ignore_errors=True)
        _tmp = None


def find_base():
    """Address (in the XM6G process) of 68k address 0, found through the reversed ring magic."""
    global _base
    pat = swap2(MAGIC)
    with open("/proc/%d/mem" % _proc.pid, "rb", 0) as mem:
        for line in open("/proc/%d/maps" % _proc.pid):
            m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)", line)
            a, b, pr = int(m[1], 16), int(m[2], 16), m[3]
            if "r" not in pr or b - a < 0x100000 or b - a > (1 << 30):
                continue
            try:
                mem.seek(a)
                d = mem.read(b - a)
            except (OSError, ValueError):
                continue
            i = d.find(pat)
            if i >= RING:
                _base = a + i - RING
                return _base
    return None


def console():
    """The console text so far (None until the kernel has started)."""
    if _proc is None or _proc.poll() is not None:
        return None
    if _base is None and find_base() is None:
        return None
    try:
        with open("/proc/%d/mem" % _proc.pid, "rb", 0) as mem:
            mem.seek(_base + RING)
            raw = swap2(mem.read(4096))
    except (OSError, ValueError):
        return None
    if raw[:8] != MAGIC:
        return None
    total = int.from_bytes(raw[8:12], "big")
    buf = raw[12:12 + 4080]
    if total <= 4080:
        data = buf[:total]
    else:
        k = total % 4080
        data = buf[k:] + buf[:k]
    return data.decode("latin-1").replace("\r\n", "\n").replace("\r", "\n")


# Which xdotool key (US layout, as the X server sees it) makes XM6G produce each character.  Wine
# turns the key into a Windows VK and XM6 maps the VK to the X68000 JIS key at the same place, so a
# few symbols come out as the neighbouring JIS character; this table is the inverse, measured on
# XM6G.  Letters, digits and the symbols not listed are typed as themselves.  '_' has no key here.
_KEY = {" ": "space", "/": "slash", ".": "period", "-": "minus", ",": "comma", "+": "plus", "|": "bar",
        "<": "less", ">": "greater", "[": "bracketleft", "]": "bracketright", "{": "braceleft", "}": "braceright",
        "$": "dollar", "#": "numbersign", "!": "exclam", "?": "question", "%": "percent", "\\": "backslash",
        ";": "equal", "*": "colon", ":": "semicolon", "(": "asterisk", ")": "parenleft", "'": "ampersand",
        '"': "at", "^": "apostrophe", "~": "quotedbl", "`": "asciitilde", "&": "asciicircum", "@": "grave",
        "=": "underscore"}


def type_line(s, retries=2):
    """Type a line.  Each key is checked against the echo (the shell echoes every character);
    a key that was dropped is pressed again."""
    for ch in s:
        for _ in range(retries + 1):
            n = len(console() or "")
            x("xdotool", "key", "--delay", "60", _KEY.get(ch, ch))
            t0 = time.time()
            while time.time() - t0 < 1.5 and len(console() or "") <= n:
                time.sleep(0.1)
            if len(console() or "") > n:
                break
    for _ in range(retries + 1):      # Return is echoed too (a newline): press again if it was dropped
        n = len(console() or "")
        x("xdotool", "key", "--delay", "60", "Return")
        t0 = time.time()
        while time.time() - t0 < 2 and len(console() or "") <= n:
            time.sleep(0.1)
        if len(console() or "") > n:
            break


def run(cmd, timeout=30):
    """Type one command, wait for the next prompt, return its output (None on timeout)."""
    before = console() or ""
    type_line(cmd)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.5)
        t = console() or ""
        if len(t) > len(before) and PROMPT.search(t) and t.count("\n") > before.count("\n"):
            lines = t[len(before):].split("\n")
            return "\n".join(lines[1:-1]) if len(lines) > 2 else ""
    return None


def key(name):
    x("xdotool", "key", "--delay", "60", name)


def waitfor(pat, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if re.search(pat, console() or ""):
            return True
        time.sleep(0.5)
    return False


def vitest():
    """vi: open a new file, insert text over two lines, :wq, read it back with cat."""
    name = "/root/vt%d" % (int(time.time()) % 100000)
    mark = len(console() or "")
    type_line("vi " + name)
    if not waitfor("-- INSERT --|" + re.escape(name), 30):
        print("FAIL  @vitest: vi did not start")
        return False
    time.sleep(1.5)
    key("i")
    if not waitfor("-- INSERT --", 20):
        print("FAIL  @vitest: no INSERT mode")
        return False
    for c in "hello":
        key(c)
        time.sleep(0.4)
    key("Return")
    time.sleep(0.5)
    for c in "world":
        key(c)
        time.sleep(0.4)
    key("Escape")
    time.sleep(1.5)
    for c in (":", "w", "q"):
        key("semicolon" if c == ":" else c)
        time.sleep(0.5)
    key("Return")
    if not waitfor(r"\x1b\[2J\x1b\[H\[[^\]\n]*\]# $", 30):
        print("FAIL  @vitest: the prompt did not come back after :wq")
        return False
    out = run("cat " + name)
    res = out is not None and "hello" in out and "world" in out
    print("%s  @vitest\t%r" % ("PASS" if res else "FAIL", out))
    return res


def tests(path):
    ok = bad = 0
    for ln in open(path, encoding="utf-8"):
        ln = ln.rstrip("\n")
        if not ln.strip() or ln.startswith("#"):
            continue
        cmd, _, exp = ln.partition("\t")
        if cmd == "@vitest":
            r = vitest()
            ok += r
            bad += not r
            continue
        out = run(cmd)
        if out is None:
            res, out = False, "(timeout)"
        elif exp == "":
            res = out.strip() == ""
        elif exp.startswith("!"):
            res = exp[1:] not in out
        elif exp.startswith("~"):
            res = re.search(exp[1:], out) is not None
        else:
            res = exp in out
        print("%s  %s\t%s%s" % ("PASS" if res else "FAIL", cmd, exp, "" if res else "   got: %r" % out[-120:]),
              flush=True)
        ok += res
        bad += not res
    print("%d passed, %d failed" % (ok, bad))
    return bad == 0


def shot(path):
    x("import", "-window", "root", path)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 2
    cmd = a[0]
    try:
        if cmd == "boot":
            return 0 if boot(a[1] if len(a) > 1 else None) else 1
        if not boot():
            return 1
        if cmd == "run":
            for c in a[1:]:
                print("# " + c)
                print(run(c))
            return 0
        if cmd == "tests":
            return 0 if tests(a[1]) else 1
        if cmd == "shot":
            shot(a[1])
            return 0
        print(__doc__)
        return 2
    finally:
        if cmd != "boot":
            stop()


if __name__ == "__main__":
    sys.exit(main())
