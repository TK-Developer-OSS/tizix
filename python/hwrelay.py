#!/usr/bin/env python3
"""hwrelay.py - 実機の回帰用。シミュレータの代わりに起動され、標準入出力を TCP で実機へ素通しする。

    python3 hwrelay.py <ポート>

  <ポート>で待ち受け、実機のシリアルを握っている中継がつないで来るのを待つ
  (Windows なら arch/<arch>/tools/hwbridge.ps1。中継は接続のたびに実機をリセットする)。
  つながったら 標準入力 → ソケット / ソケット → 標準出力 をそのまま流す。
  テストがこのプロセスを kill すればソケットが閉じ、中継は次の接続を待つ。
  使い方は tzpaths.py の TIZIX_HW を参照。
"""
import os
import select
import socket
import sys

port = int(sys.argv[1])
ls = socket.socket()
ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("0.0.0.0", port))
ls.listen(1)
ls.settimeout(60)
try:
    s, _ = ls.accept()
except socket.timeout:
    sys.stderr.write("hwrelay: 60 秒待っても中継がつながらない(hwbridge は動いている?)\n")
    sys.exit(1)
ls.close()
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

fin, fout = sys.stdin.fileno(), sys.stdout.fileno()
# テストは pty の向こうにいる。端末の行規律(エコー・CR→LF・LF→CRLF・Ctrl+C の
# シグナル化)を切っておく。シミュレータ(QEMU の stdio chardev 等)も同じことをしている。
if os.isatty(fin):
    import tty
    tty.setraw(fin)
while True:
    r, _, _ = select.select([fin, s], [], [])
    if fin in r:
        d = os.read(fin, 4096)
        if not d:
            break
        s.sendall(d)
    if s in r:
        d = s.recv(4096)
        if not d:
            break
        os.write(fout, d)
s.close()
