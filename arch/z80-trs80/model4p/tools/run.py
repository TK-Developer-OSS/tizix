import os, pty, subprocess, sys, select, tty, termios
xtrs, disp, T = sys.argv[1:4]
m, s = pty.openpty()
tty.setraw(m); tty.setraw(s)
p = subprocess.Popen([xtrs, "-model", "4p", "-diskdir", T, "-serial", os.ttyname(s)], env=dict(os.environ, DISPLAY=disp))
old = termios.tcgetattr(0); tty.setraw(0)
try:
    while p.poll() is None:
        r, _, _ = select.select([0, m], [], [], 0.5)
        if m in r: os.write(1, os.read(m, 4096))
        if 0 in r:
            d = os.read(0, 1024)
            if b"\x1d" in d: break          # ctrl-]
            os.write(m, d)
finally:
    termios.tcsetattr(0, termios.TCSADRAIN, old); p.kill()
