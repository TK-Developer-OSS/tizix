#!/usr/bin/env python3
"""t105_update.py - 実機ハーネス(T105 など)の tizix を、シリアルだけで新しい像に更新する。フロッピーの差し替えは要らない。

  使い方:
    sudo python3 tools/t105_update.py [--dev /dev/ttyS0 | --qemu IMG] [--image NEW.img] [--files /bin/a.bin ...] [--kernel] [--reboot]
      --dev DEV      つなぎ先のシリアル(既定 /dev/ttyS0、115200 8N1)
      --qemu IMG     試験用: シリアルの代わりに IMG で QEMU を起動してつなぐ(リセットで落ちないよう -no-reboot なし)
      --image NEW    新しい像(既定 disk/floppy_tz.img)。送るファイルと予約領域はここから取り出す
      --files P...   像の中のこのパスのファイルを、同じパスへ xmodem で送る(例: /bin/ls.bin /etc/rc)
      --put L:R ...  手元のファイル L を tizix の R へ送る(像には入れない、その機械だけの設定。例: T105 専用の /etc/rc)
      --changed      像の /bin と /etc で、前回このスクリプトで送ったものから変わったファイルを全部送る(記録は tools/.t105_sent.json)
      --kernel       予約領域(ブートセクタ + stage2 + カーネル)を送って sysup で書き換える(次の再起動から有効)
      --reboot       最後に reboot して、プロンプトが戻るのを待つ
  手順: プロンプトを確かめる → (ファイルごとに)rx PATH を打ってから sx で送る → (--kernel)/sysup.img を送って
        sysup → rm → (--reboot)reboot して起動を待つ。tizix 側の rx・sysup・reboot が要る(この版の像には入っている)。
  xmodem(lrzsz の sx)はブロックごとに ACK を待つので、実機の遅いフロッピーでも落ちない。"""
import argparse, hashlib, json, os, pty, select, subprocess, sys, tempfile, termios, time

HERE = os.path.dirname(os.path.abspath(__file__))
ARCH = os.path.dirname(HERE)
FAT_RSVD = 160                     # Makefile の FAT_RSVD(予約領域のセクタ数)

class Link:
    def __init__(self, dev=None, qemu=None):
        self.p = None
        if qemu:
            m, s = pty.openpty()
            self.p = subprocess.Popen(["qemu-system-i386", "-m", "128", "-drive", f"file={qemu},format=raw,if=floppy",
                                       "-chardev", "stdio,id=c0,signal=off", "-serial", "chardev:c0",
                                       "-display", "none", "-boot", "a"], stdin=s, stdout=s, stderr=s)
            os.close(s)
            self.fd = m
        else:
            self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
            a = termios.tcgetattr(self.fd)
            a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
            a[4] = a[5] = termios.B115200
            a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, a)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = b""

    def read_until(self, pat, t):
        end = time.time() + t
        start = len(self.buf)
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                try: self.buf += os.read(self.fd, 4096)
                except OSError: return False
            if pat.encode() in self.buf[start:]: return True
        return False

    def send_line(self, line):
        for ch in line + "\r":
            os.write(self.fd, ch.encode()); time.sleep(0.02)

    def cmd(self, line, t=30):
        n = len(self.buf)
        self.send_line(line)
        ok = self.read_until("]# ", t)
        return ok, self.buf[n:].decode(errors="replace").replace("\r", "")

    def close(self):
        if self.p:
            self.p.kill(); self.p.wait()
        os.close(self.fd)

def mtype(img, path):
    return subprocess.run(["mtype", "-i", img, "::" + path], capture_output=True, check=True).stdout

def mlist(img, d):
    out = subprocess.run(["mdir", "-b", "-i", img, "::" + d], capture_output=True, text=True).stdout
    return [l.strip().replace("::", "", 1) for l in out.splitlines() if l.strip() and not l.strip().endswith("/")]

def xsend(link, path, data, log):
    """tizix で rx PATH を走らせ、data を sx で送る。"""
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(data); src = f.name
    try:
        link.send_line("rx " + path)
        time.sleep(1.0)
        t0 = time.time()
        r = subprocess.run(["sx", "-q", src], stdin=link.fd, stdout=link.fd, stderr=subprocess.PIPE, timeout=1800)
        ok = r.returncode == 0 and link.read_until("]# ", 60)
        log(f"  {path}: {len(data)} bytes, {time.time() - t0:.1f}s, {'ok' if ok else 'FAILED'}")
        return ok
    finally:
        os.unlink(src)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dev", default="/dev/ttyS0")
    ap.add_argument("--qemu")
    ap.add_argument("--image", default=os.path.join(ARCH, "disk", "floppy_tz.img"))
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--put", nargs="*", default=[])
    ap.add_argument("--changed", action="store_true")
    ap.add_argument("--kernel", action="store_true")
    ap.add_argument("--reboot", action="store_true")
    a = ap.parse_args()
    log = lambda s: print(s, flush=True)

    link = Link(dev=None if a.qemu else a.dev, qemu=a.qemu)
    try:
        if a.qemu:
            if not link.read_until("[/root]# ", 30): log("no prompt (qemu)"); return 1
        else:
            link.send_line("")
            if not link.read_until("]# ", 10): log("no prompt on " + a.dev); return 1
        link.cmd("cd /root")
        files = list(a.files)
        state_path = os.path.join(HERE, ".t105_sent.json")
        sent = {}
        if a.changed:
            if os.path.exists(state_path):
                sent = json.load(open(state_path))
            for d in ("/bin", "/etc"):
                for p in mlist(a.image, d):
                    h = hashlib.md5(mtype(a.image, p)).hexdigest()
                    if sent.get(p) != h and p not in files:
                        files.append(p)
        fails = 0
        for p in files:
            data = mtype(a.image, p)
            if xsend(link, p, data, log):
                sent[p] = hashlib.md5(data).hexdigest()
            else:
                fails += 1
        for pr in a.put:
            local, remote = pr.split(":", 1)
            if not xsend(link, remote, open(local, "rb").read(), log):
                fails += 1
        if a.changed and not a.qemu:
            json.dump(sent, open(state_path, "w"), indent=0)
        if a.kernel:
            boot = open(a.image, "rb").read()[:FAT_RSVD * 512]
            while boot.endswith(b"\0" * 512):        # 予約領域の後ろの空きセクタは送らない
                boot = boot[:-512]
            if not xsend(link, "/sysup.img", boot, log):
                log("kernel: transfer failed, nothing written"); return 1
            ok, out = link.cmd("sysup /sysup.img", 600)
            log("  " + out.strip().replace("\n", "\n  "))
            link.cmd("rm /sysup.img")
            if "verified" not in out:
                log("kernel: sysup failed"); return 1
        if a.reboot:
            link.send_line("reboot")
            if not link.read_until("[/root]# ", 120): log("reboot: no prompt within 120 s"); return 1
            b = link.buf.decode(errors="replace")
            for l in b[b.rfind("cpu:"):].splitlines()[:3]:
                log("  " + l.strip())
            log("reboot: back at the prompt")
        return 1 if fails else 0
    finally:
        link.close()

if __name__ == "__main__":
    sys.exit(main())
