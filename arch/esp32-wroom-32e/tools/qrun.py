#!/usr/bin/env python3
"""qrun.py - QEMU(-machine esp32)で tizix を起動し、コマンドを順に打って端末出力を出す

    qrun.py FLASH.bin [-t 秒] CMD...

  プロンプト("# ")を待ってから 1 行ずつ打つ。-t は 1 コマンドの待ち時間の上限(既定 8 秒)。
  "RAW:" で始まる引数はプロンプトを待たずにそのまま送る(\\r \\x1b \\x03 などを解釈、
  送ったあと 1 秒置く)。vi の操作や Ctrl+C はこれで打つ:

    qrun.py flash.bin 'ls /bin' 'RAW:a\\r' 'RAW:\\x03' 'RAW:vi v.txt\\r' 'RAW:ihello\\x1b' 'RAW::wq\\r' 'cat v.txt'

  flash への書き込みは FLASH.bin の隣の写し(.run)に行うので、元の像は変わらない。
"""
import shutil
import subprocess
import sys
import threading
import time


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    flash = args.pop(0)
    tmo = 8.0
    if args and args[0] == "-t":
        args.pop(0)
        tmo = float(args.pop(0))

    work = flash + ".run"
    shutil.copyfile(flash, work)
    p = subprocess.Popen(
        ["qemu-system-xtensa", "-display", "none", "-machine", "esp32",
         "-drive", "file=%s,if=mtd,format=raw" % work,
         "-serial", "stdio", "-monitor", "none"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    buf = bytearray()

    def rd():
        while True:
            b = p.stdout.read(1)
            if not b:
                break
            buf.extend(b)

    threading.Thread(target=rd, daemon=True).start()

    def send(data, gap):
        for ch in data:
            p.stdin.write(bytes([ch]))
            p.stdin.flush()
            time.sleep(gap)

    def wait_prompt(start, t):
        end = time.time() + t
        while time.time() < end:
            if b"# " in buf[start:]:
                time.sleep(0.2)
                return True
            time.sleep(0.05)
        return False

    t0 = time.time()
    ok = wait_prompt(0, 15)
    print("[boot %.1fs prompt=%s]" % (time.time() - t0, ok))
    for c in args:
        start = len(buf)
        t1 = time.time()
        if c.startswith("RAW:"):
            send(c[4:].encode().decode("unicode_escape").encode("latin-1"), 0.02)
            time.sleep(1.0)
            print("[%r sent]" % c)
            continue
        send((c + "\r").encode(), 0.01)
        ok = wait_prompt(start + len(c), tmo)
        print("[%r %.2fs prompt=%s]" % (c, time.time() - t1, ok))
    time.sleep(0.3)
    p.kill()
    sys.stdout.write("---- console ----\n" + buf.decode("latin-1").replace("\r", "") + "\n")


if __name__ == "__main__":
    main()
