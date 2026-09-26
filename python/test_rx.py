#!/usr/bin/env python3
# test_rx.py - rx(XMODEM 受信)をホスト側の XMODEM 送信で検証する(#58)
#
#   使い方: python3 python/test_rx.py [z80pack|z80board]   (省略時は TIZIX_ARCH)
#   ゲストで `rx /root/rx.txt` を起動し、ホストが送信側になって CRC モードの
#   128B パケットで送る。受信後に cat で中身を突き合わせる(複数パケットで
#   中身が 1 パケットずれる / 途中で止まる、の両方を検出する)。
import os
import sys
import pty
import time
import select
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) > 1:
    os.environ["TIZIX_ARCH"] = sys.argv[1]
import tzpaths  # noqa: E402  (TIZIX_ARCH を見るので上で設定してから)

SOH, EOT, ACK, NAK, CAN = 0x01, 0x04, 0x06, 0x15, 0x18

# 10 パケット強(1300B 程度)の行データ。最後のパケットは 0x1A で埋まる。
lines = ["line %03d abcdefghijklmnopqrstuvwxyz" % i for i in range(40)]
payload = ("\n".join(lines) + "\n").encode()

tzpaths.wait_disk_ready(tzpaths.DRIVEB)
master, slave = pty.openpty()
proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
                        stdin=slave, stdout=slave, stderr=slave, close_fds=True)
os.close(slave)


def read_until(pat, timeout):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.05)
        if master in r:
            try:
                c = os.read(master, 4096)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat in buf:
                break
    return buf


seen = bytearray()      # wait_byte が読んだ全バイト(後の判定で使う。読み捨てない)


def wait_byte(want, timeout):
    """want(集合)のどれかが来るまで読む。来たバイトを返す(来なければ None)。
    読んだバイトは seen に残す ── ACK と同じ読み込みに rx の完了メッセージが
    入ってくることがあり、捨てるとタイミング次第で FAIL した。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.05)
        if master in r:
            data = os.read(master, 4096)
            for i, b in enumerate(data):
                if b in want:
                    seen.extend(data[i + 1:])
                    return b
            seen.extend(data)
    return None


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + detail))
    if not cond:
        fails += 1


read_until(b"]# ", 30)
os.write(master, b"rm /root/rx.txt\r")
read_until(b"]# ", 10)
os.write(master, b"rx /root/rx.txt\r")

ok = wait_byte({ord("C")}, 20) is not None
check("rx が CRC モードの 'C' を出す", ok)

blocks = [payload[i:i + 128] for i in range(0, len(payload), 128)]
seq = 1
sent_all = ok
t_start = time.time()
for i, blk in enumerate(blocks):
    if not sent_all:
        break
    data = blk + bytes([0x1A]) * (128 - len(blk))
    c = crc16(data)
    pkt = bytes([SOH, seq & 0xFF, 0xFF - (seq & 0xFF)]) + data + bytes([c >> 8, c & 0xFF])
    r = None
    for attempt in range(5):
        os.write(master, pkt)
        r = wait_byte({ACK, NAK, CAN}, 10)
        if r == ACK:
            break
        print("  block %d: %s (attempt %d)" % (i + 1, {NAK: "NAK", CAN: "CAN", None: "no reply"}[r], attempt + 1))
        if r == CAN:
            break
    if r != ACK:
        sent_all = False
        check("block %d/%d が ACK される" % (i + 1, len(blocks)), False)
        break
    seq += 1
if sent_all:
    os.write(master, bytes([EOT]))
    seen.clear()
    r = wait_byte({ACK, NAK}, 10)
    check("EOT に ACK", r == ACK, repr(r))
    print("  %d blocks in %.1fs" % (len(blocks), time.time() - t_start))
    out = bytes(seen)
    if b"]# " not in out:
        out += read_until(b"]# ", 15)
    check("rx: received successfully", b"received successfully" in out, repr(out[-120:]))

    os.write(master, b"cat /root/rx.txt\r")
    out = read_until(b"]# ", 20).replace(b"\r", b"").split(b"\n")
    body = [l.decode("latin-1") for l in out[1:-1]]
    check("中身が送ったものと一致(%d 行)" % len(lines), body == lines,
          "got %d lines, first=%r last=%r" % (len(body), body[:1], body[-1:]))
else:
    os.write(master, bytes([CAN, CAN, CAN]))

proc.kill()
print("RESULT:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
sys.exit(1 if fails else 0)
