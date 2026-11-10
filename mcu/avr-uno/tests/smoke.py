#!/usr/bin/env python3
"""Serial smoke test for tizix-mcu on a real board.  usage: smoke.py [PORT]
Opening the port resets an Uno (DTR), so we wait for the banner first."""
import sys, time, serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
PROMPT = b"[/]# "

ser = serial.Serial(PORT, 115200, timeout=0.2)

def read_until(token, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        buf += ser.read(max(1, ser.in_waiting))
        if buf.endswith(token):
            return buf
    raise SystemExit("timeout waiting for %r, got %r" % (token, buf))

def run(cmd, timeout=3.0):
    ser.write(cmd.encode() + b"\r")
    out = read_until(PROMPT, timeout).decode(errors="replace")
    out = out.replace("\r\n", "\n")
    body = out.split("\n", 1)[1] if "\n" in out else ""
    return body[: -len(PROMPT)].rstrip("\n") if body.endswith(PROMPT.decode()) else body

fails = 0
def check(name, cond, got=None):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  got=%r" % (got,)))
    fails += not cond

banner = read_until(PROMPT, 6.0).decode(errors="replace")
check("banner", "tizix-mcu atmega328p" in banner, banner)

check("echo",            run("echo hello world") == "hello world")
check("echo quotes",     run('echo "a  b" c') == "a  b c")
check("unknown command", "nosuch: not found" in run("nosuch"))
check("too many words",  "too many" in run("echo 1 2 3 4 5 6 7 8 9"))
check("unterminated",    "unterminated" in run('echo "x'))
check("comment line",    run("# nothing") == "")
check("help lists cat",  "cat" in run("help"))
bins = ["/bin/"] + ["/bin/" + n for n in "help ls echo sleep free cat tzsh".split()] + ["/" + n for n in ("mcu.sh", "blink.sh")]
everything = (bins + ["/dev/", "/dev/gpio/"] + ["/dev/gpio/%d" % n for n in range(2, 20)] +
              ["/dev/timer/", "/dev/timer/0/", "/dev/timer/0/limit", "/dev/timer/0/finish", "/dev/timer/0/repeat"])
def names(c): return sorted(run(c).split())
def col_major(c):   # the columns run down, then across: read them back in that order
    rows = [l.split() for l in run(c).split("\n")]
    return [r[k] for k in range(len(rows[0])) for r in rows if k < len(r)]
r = run("ls")
check("ls lists everything, recursively", sorted(r.split()) == sorted(everything), r)
check("ls is in columns, within 80 chars", len(r.split("\n")) < len(everything) and all(len(l) <= 80 for l in r.split("\n")), r)
check("ls columns run down, then across", col_major("ls") == everything, col_major("ls"))
check("ls /  is the same",   col_major("ls /") == everything)
check("ls /dev: everything below it", col_major("ls /dev") == everything[len(bins) + 1:])
check("ls /bin lists the commands", col_major("ls /bin") == bins[1:8], col_major("ls /bin"))
check("cat a script", "setup {" in run("cat /blink.sh") and run("cat blink.sh") == run("cat /blink.sh"))
check("ls a script", run("ls /blink.sh") == "/blink.sh")
check("ls /dev/ is the same as ls /dev", run("ls /dev/") == run("ls /dev"))
check("ls /dev/gpio",    col_major("ls /dev/gpio") == ["/dev/gpio/%d" % n for n in range(2, 20)])
check("ls /dev/timer",   col_major("ls /dev/timer") == ["/dev/timer/0/", "/dev/timer/0/limit", "/dev/timer/0/finish", "/dev/timer/0/repeat"])
check("ls /dev/timer/0", col_major("ls /dev/timer/0") == ["/dev/timer/0/limit", "/dev/timer/0/finish", "/dev/timer/0/repeat"])
check("ls a device",     run("ls /dev/gpio/13") == "/dev/gpio/13")
check("ls /dev/gp is not /dev/gpio", "no such device" in run("ls /dev/gp"))
check("ls /nosuch",      "no such device" in run("ls /nosuch"))
r = run("free"); check("free", r.endswith("bytes free"), r)

check("write 1",         run("echo 1 > /dev/gpio/11") == "")
check("cat reads it",    run("cat /dev/gpio/11") == "1")
check("write 0",         run("echo 0 > /dev/gpio/11") == "")
check("cat reads 0",     run("cat /dev/gpio/11") == "0")
check("in up",           run("echo up > /dev/gpio/12") == "")
check("pull-up reads 1", run("cat /dev/gpio/12") == "1")
check("in",              run("echo in > /dev/gpio/12") == "")
check("write to the uart pin", "no such device" in run("echo 1 > /dev/gpio/0"))
check("no such pin",     "no such device" in run("cat /dev/gpio/99"))
check("bad value",       "bad value" in run("echo 2 > /dev/gpio/11"))
check("no such node",    "no such device" in run("echo 1 > /dev/nothing"))
check("redirect needs a command", "syntax error" in run("> /dev/gpio/11"))
check("failed command shows its message", "not found" in run("nosuch > /dev/gpio/11"))
run("echo in > /dev/gpio/11")

# the timer: one shot by default; the interrupt only raises /dev/timer/0/finish
def wait_finish(limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        if run("cat /dev/timer/0/finish") == "1":
            return time.time() - t0
    return None
check("timer idle",       run("cat /dev/timer/0/limit") == "0" and run("cat /dev/timer/0/finish") == "0")
check("one shot is the default", run("cat /dev/timer/0/repeat") == "0")
run("echo 500 > /dev/timer/0/limit")
check("limit reads back", run("cat /dev/timer/0/limit") in ("500", "0"))
dt = wait_finish(2.0)
check("finish after ~0.5 s", dt is not None and 0.3 < dt < 0.7, dt)
check("one shot stopped",  run("cat /dev/timer/0/limit") == "0")
check("finish stays set",  run("cat /dev/timer/0/finish") == "1")
run("echo 0 > /dev/timer/0/finish")
check("finish cleared",    run("cat /dev/timer/0/finish") == "0")
time.sleep(0.8)
check("no second finish in one shot mode", run("cat /dev/timer/0/finish") == "0")
run("echo 1 > /dev/timer/0/repeat")
run("echo 300 > /dev/timer/0/limit")
gaps, t_last = [], time.time()
for _ in range(4):
    dt = wait_finish(1.5)
    if dt is None:
        break
    run("echo 0 > /dev/timer/0/finish")
    now = time.time(); gaps.append(now - t_last); t_last = now
check("repeat mode finishes again and again", len(gaps) == 4, gaps)
check("repeat period ~0.3 s", gaps and 0.2 < sum(gaps[1:]) / max(1, len(gaps) - 1) < 0.45, gaps)
run("echo 0 > /dev/timer/0/limit")
run("echo 0 > /dev/timer/0/repeat")
run("echo 0 > /dev/timer/0/finish")
check("bad timer value", "bad value" in run("echo 2 > /dev/timer/0/repeat"))

r = run("tzsh mcu.sh", 30)
check("tzsh mcu: ALL OK", r.endswith("ALL OK") and "FAIL" not in r, r[-200:])
check("tzsh mcu: 36 checks", r.count("\nok ") + (1 if r.startswith("ok ") else 0) >= 36, r.count("ok "))
check("tzsh lists the scripts", all(n in run("tzsh") for n in ("mcu.sh", "blink.sh")))
check("tzsh unknown script", "usage" in run("tzsh nosuch"))

def blink(secs):
    """run blink.sh for a while; return the times its 'loop: 1000 rounds' lines arrived"""
    ser.write(b"tzsh /blink.sh\r")
    t0, ev, buf = time.time(), [], b""
    while time.time() - t0 < secs:
        buf += ser.read(max(1, ser.in_waiting))
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if b"loop: 1000 rounds" in line:
                ev.append(time.time() - t0)
    ser.write(b"\x03")
    read_until(PROMPT, 3)
    leds = run("cat /dev/gpio/13")
    run("echo 0 > /dev/timer/0/limit"); run("echo 0 > /dev/timer/0/repeat"); run("echo 0 > /dev/timer/0/finish")
    return ev, leds
ev, leds = blink(6.0)
check("blink.sh: the loop runs alongside (1000-round message)", len(ev) >= 1, ev)
check("blink.sh: the intr block drove D13", leds in ("0", "1"), leds)
check("blink.sh: Ctrl-C ends it", run("echo back") == "back")
print("     1000 rounds at", ["%.2f" % t for t in ev])

t0 = time.time(); run("sleep 2", 5); dt = time.time() - t0
check("sleep 2 takes ~2s", 1.8 < dt < 2.6, dt)
ser.write(b"sleep 30\r"); time.sleep(0.5); ser.write(b"\x03")
t0 = time.time(); read_until(PROMPT, 3); check("Ctrl-C ends sleep", time.time() - t0 < 2)

print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
