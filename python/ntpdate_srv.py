#!/usr/bin/env python3
"""ntpdate_srv.py - user/ntpdate.c 用の簡易 JST 時刻サーバ。

接続を受けたら即座に "TIME <epoch_jst>\n" を送って閉じる(リクエスト不要の
能動送信、RFC868 Time Protocol に近い最小プロトコル)。epoch_jst は本物の
UTC epoch に 9 時間を加算した値 ── tizix にタイムゾーン概念は無く、
KW_EPOCH_SEC は「そのまま date で表示すれば正しく見える」値を保持する方針
なので、ここでも UTC+9 を焼き込んで渡す。

使い方:
    python3 ntpdate_srv.py [port]         (既定 2123)

z80pack 側からは python/at_modem.py(ESP-AT テストベッド、127.0.0.1:8080)を
先に起動しておき、`net &` の後に `ntpdate` を叩くと
  ntpdate -> net.bin -> (ATD) -> at_modem.py -> (AT+CIPSTART) -> ここ
という経路で 1 行受信する。
"""
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2123
JST_OFFSET = 9 * 3600


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", PORT))
        s.listen(5)
        print(f"[*] ntpdate JST time server on 127.0.0.1:{PORT}")
        while True:
            conn, addr = s.accept()
            try:
                epoch_jst = int(time.time()) + JST_OFFSET
                conn.sendall(f"TIME {epoch_jst}\n".encode())
                print(f"[*] served TIME {epoch_jst} to {addr}")
            except Exception as e:
                print(f"[!] error serving {addr}: {e}")
            finally:
                conn.close()


if __name__ == "__main__":
    main()
