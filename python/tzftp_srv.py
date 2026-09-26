#!/usr/bin/env python3
"""Minimal host-side file server for tizix's `tzftp` guest command.

Not real FTP (RFC 959) -- a tiny line-based protocol layered directly on
the net.c/netcli.c raw byte relay (see DEVELOP.md 5.6 and
python/_debug_nettest2.py for the underlying transport's known issues).
One TCP connection per request; this process closes the socket itself
when done since net_close() on the guest side does not actually close
the host socket (net.c only clears local state -- see user/net.c).
(Renamed from ftp_srv.py on 2026-09-17 to make clear this is not the
real FTP protocol.)

Protocol (ASCII, '\n'-terminated header line):
  PUT <name> <size>\n<size raw bytes>   -- guest -> host, saved under --root
  GET <name>\n                          -- host -> guest: "SIZE <n>\n" + bytes
                                            (n=0 and immediate close if missing)

Usage: tzftp_srv.py [--host 127.0.0.1] [--port 2121] [--root ./tzftp_root]
"""
import argparse
import os
import socket
import sys
import threading


def recv_line(conn, limit=256):
    buf = b""
    while len(buf) < limit:
        c = conn.recv(1)
        if not c:
            break
        if c == b"\n":
            break
        buf += c
    return buf.decode("ascii", "replace")


def recv_exact(conn, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(min(4096, n - len(buf)))
        if not chunk:
            break
        buf += chunk
    return bytes(buf)


def handle(conn, addr, root):
    # 2026-09-17: 元は完全同期(1接続ずつ処理)だったため、1リクエストが
    # recv() でスタックすると以後のすべての接続がTCPレベルでは
    # accept()待ちのままキューに積まれ続け、guest 側の net_connect() は
    # (OSレベルの3-way handshakeが成立するだけで)「成功」と誤認したまま
    # 実際には誰にも処理されない、という事故を踏んだ。ソケットに
    # タイムアウトを設定し、main() 側もスレッド化して1接続のスタックが
    # 他の接続を巻き込まないようにする。
    conn.settimeout(15)
    try:
        line = recv_line(conn)
        print(f"[tzftp_srv] {addr}: {line!r}")
        parts = line.split()
        if not parts:
            return
        cmd = parts[0].upper()
        if cmd == "PUT" and len(parts) == 3:
            name, size_s = parts[1], parts[2]
            try:
                size = int(size_s)
            except ValueError:
                print(f"[tzftp_srv] bad size: {size_s!r}")
                return
            data = recv_exact(conn, size)
            if len(data) != size:
                print(f"[tzftp_srv] PUT {name}: short read {len(data)}/{size}")
            path = os.path.join(root, os.path.basename(name))
            with open(path, "wb") as f:
                f.write(data)
            print(f"[tzftp_srv] PUT {name}: saved {len(data)} bytes -> {path}")
        elif cmd == "GET" and len(parts) == 2:
            name = parts[1]
            path = os.path.join(root, os.path.basename(name))
            if not os.path.isfile(path):
                print(f"[tzftp_srv] GET {name}: not found ({path})")
                conn.sendall(b"SIZE 0\n")
                return
            with open(path, "rb") as f:
                data = f.read()
            conn.sendall(f"SIZE {len(data)}\n".encode("ascii"))
            conn.sendall(data)
            print(f"[tzftp_srv] GET {name}: sent {len(data)} bytes")
        else:
            print(f"[tzftp_srv] unrecognized request: {line!r}")
    except Exception as e:
        print(f"[tzftp_srv] error handling {addr}: {e}")
    finally:
        conn.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--root", default="./tzftp_root")
    args = ap.parse_args()

    os.makedirs(args.root, exist_ok=True)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(4)
    print(f"[tzftp_srv] listening on {args.host}:{args.port}, root={args.root}")
    sys.stdout.flush()

    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle, args=(conn, addr, args.root), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()


if __name__ == "__main__":
    main()
