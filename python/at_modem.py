"""at_modem.py - ESP32 AT ファームウェア(ESP-AT)の簡易エミュレータ。

想定シナリオ: z80board が ESP32 に SPI bit-bang で接続し、ESP-AT コマンド
セット(AT+CWJAP / AT+CIPSTART / AT+CIPSEND / +IPD 等)を叩いて WiFi 経由の
TCP 通信を行う。実機の ESP32 がまだ無くても、z80board 側(または将来の
net.c 相当のドライバ)の AT コマンド送受信シーケンスを先行してテストできる
よう、TCP 越しに ESP-AT の主要コマンドへ応答するテストベッドとして作った。

実際の ESP32 と違い、WiFi 接続はすべて即座に成功したふりをする
(AT+CWJAP は実際には何もしない)。AT+CIPSTART で指定された host:port には
**本当に TCP 接続**し、AT+CIPSEND で送ったデータは実際にそこへ送信、
相手から届いたデータは ESP-AT 標準の `+IPD,<len>:<data>` 形式で
非同期に通知する(実機と同じ挙動)。

対応コマンド(1 接続のみ、シングルコネクション前提。CIPMUX=0 相当):
    AT                              -> OK
    ATE0 / ATE1                     -> OK(エコー無効/有効、動作は変えない)
    AT+RST                          -> OK(状態リセットするふり)
    AT+GMR                          -> バージョン文字列 + OK
    AT+CWMODE=<n>                   -> OK
    AT+CWJAP="ssid","pass"          -> WIFI CONNECTED / WIFI GOT IP / OK
    AT+CIFSR                        -> +CIFSR:STAIP,"..." / +CIFSR:STAMAC,"..." / OK
    AT+CIPSTATUS                    -> STATUS:<n> / OK
    AT+CIPSTART="TCP","host",port   -> 実際に connect()。CONNECT / OK か ERROR
    AT+CIPSEND=<n>                  -> OK して "> " プロンプト → 続く n バイトを
                                        実送信 → SEND OK
    AT+CIPCLOSE                     -> 実ソケットを close。CLOSED / OK
    上記以外                        -> ERROR

相手からの受信は非同期スレッドが監視し、`+IPD,<len>:<raw bytes>` として
クライアント(z80board/net.c 側)へそのまま流す。

使い方:
    python3 at_modem.py                    # 既定 8080 で待受
    python3 at_modem.py --port 8080
"""
import argparse
import re
import socket
import threading

FW_VERSION = (
    "AT version:2.4.0.0(at_modem.py test stub)\r\n"
    "SDK version:test\r\n"
    "compile time:2026-09-17\r\n"
    "Bin version:1.0.0(TIZIX-TESTBED)\r\n"
)

CIPSTART_RE = re.compile(
    r'^AT\+CIPSTART="(TCP|UDP)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*$', re.I)
CIPSEND_RE = re.compile(r'^AT\+CIPSEND=(\d+)\s*$', re.I)
CWJAP_RE = re.compile(r'^AT\+CWJAP="([^"]*)"\s*,\s*"([^"]*)"\s*$', re.I)
CWMODE_RE = re.compile(r'^AT\+CWMODE(?:_\w+)?=(\d+)\s*$', re.I)


class Session:
    """1 クライアント接続 = 1 ESP32 相当。tcp_sock が nullptr でなければ
    AT+CIPSTART 済み(実ソケット保持中)。"""

    def __init__(self, client_sock, addr):
        self.client = client_sock
        self.addr = addr
        self.tcp_sock = None
        self.rx_thread = None
        self.closing = False
        self.lock = threading.Lock()

    def send(self, text):
        try:
            self.client.sendall(text.encode("utf-8", errors="ignore"))
        except Exception:
            pass

    def log(self, msg):
        print(f"[{self.addr}] {msg}")

    # ---- +IPD 非同期通知スレッド -------------------------------------
    def start_rx_forwarder(self):
        def worker():
            sock = self.tcp_sock
            while not self.closing and sock is not None:
                try:
                    data = sock.recv(4096)
                except Exception:
                    break
                if not data:
                    break
                self.log(f"+IPD {len(data)} bytes <- target")
                with self.lock:
                    self.send(f"\r\n+IPD,{len(data)}:")
                    try:
                        self.client.sendall(data)
                    except Exception:
                        break
            # 相手側が切断した
            if not self.closing:
                self.log("target closed connection")
                with self.lock:
                    self.tcp_sock = None
                    self.send("\r\nCLOSED\r\n")
        self.rx_thread = threading.Thread(target=worker, daemon=True)
        self.rx_thread.start()

    # ---- AT コマンド処理 ------------------------------------------------
    def handle_line(self, line):
        line = line.strip()
        if not line:
            return
        self.log(f"AT<< {line!r}")
        upper = line.upper()

        if upper == "AT":
            self.send("\r\nOK\r\n")
        elif upper in ("ATE0", "ATE1"):
            self.send("\r\nOK\r\n")
        elif (upper.startswith("AT+UART_CUR=") or upper.startswith("AT+UART_DEF=")
              or upper.startswith("AT+CIPMUX=")):
            # 実機の ESP-AT 1.3 はどれも受け付ける(#62: net が起動時に
            # CTS フロー制御 AT+UART_CUR=9600,8,1,0,2 と単一接続 CIPMUX=0 を送る)。
            self.send("\r\nOK\r\n")
        elif upper == "AT+RST":
            with self.lock:
                if self.tcp_sock:
                    try:
                        self.tcp_sock.close()
                    except Exception:
                        pass
                    self.tcp_sock = None
            self.send("\r\nOK\r\n")
        elif upper == "AT+GMR":
            self.send("\r\n" + FW_VERSION + "\r\nOK\r\n")
        elif CWMODE_RE.match(line):
            self.send("\r\nOK\r\n")
        elif CWJAP_RE.match(line):
            m = CWJAP_RE.match(line)
            self.log(f"(simulated) joining AP {m.group(1)!r}")
            self.send("\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n\r\nOK\r\n")
        elif upper == "AT+CIFSR":
            self.send(
                '\r\n+CIFSR:STAIP,"192.168.4.2"\r\n'
                '+CIFSR:STAMAC,"aa:bb:cc:dd:ee:ff"\r\n\r\nOK\r\n')
        elif upper == "AT+CIPSTATUS":
            with self.lock:
                st = 3 if self.tcp_sock else 2
            self.send(f"\r\nSTATUS:{st}\r\n\r\nOK\r\n")
        elif CIPSTART_RE.match(line):
            m = CIPSTART_RE.match(line)
            proto, host, port = m.group(1), m.group(2), int(m.group(3))
            with self.lock:
                if self.tcp_sock:
                    self.send("\r\nERROR\r\nALREADY CONNECTED\r\n")
                    return
                self.log(f"CIPSTART -> connecting to {host}:{port} ({proto})")
                try:
                    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    s.settimeout(10)
                    s.connect((host, port))
                    s.settimeout(None)
                    self.tcp_sock = s
                except Exception as e:
                    self.log(f"CIPSTART failed: {e}")
                    self.send("\r\nERROR\r\n")
                    return
            self.send("\r\nCONNECT\r\n\r\nOK\r\n")
            self.start_rx_forwarder()
        elif CIPSEND_RE.match(line):
            m = CIPSEND_RE.match(line)
            n = int(m.group(1))
            with self.lock:
                if not self.tcp_sock:
                    self.send("\r\nERROR\r\nNOT CONNECTED\r\n")
                    return
            self.send("\r\nOK\r\n> ")
            payload = self.recv_exact(n)
            if payload is None:
                self.send("\r\nSEND FAIL\r\n")
                return
            with self.lock:
                try:
                    self.tcp_sock.sendall(payload)
                except Exception as e:
                    self.log(f"CIPSEND write failed: {e}")
                    self.send("\r\nSEND FAIL\r\n")
                    return
            self.log(f"CIPSEND {n} bytes -> target: {payload!r}")
            self.send("\r\nSEND OK\r\n")
        elif upper == "AT+CIPCLOSE":
            with self.lock:
                if self.tcp_sock:
                    try:
                        self.tcp_sock.close()
                    except Exception:
                        pass
                    self.tcp_sock = None
                    self.send("\r\nCLOSED\r\n\r\nOK\r\n")
                else:
                    self.send("\r\nERROR\r\nNOT CONNECTED\r\n")
        else:
            self.log(f"unrecognized command: {line!r}")
            self.send("\r\nERROR\r\n")

    def recv_exact(self, n):
        """AT+CIPSEND=<n> の直後、生バイトを n 個だけ読み込む
        (残りバッファ self._pending があればそこから優先して消費)。"""
        buf = bytearray()
        while len(buf) < n:
            try:
                chunk = self.client.recv(n - len(buf))
            except Exception:
                return None
            if not chunk:
                return None
            buf += chunk
        return bytes(buf)


def handle_client(client_sock, addr):
    sess = Session(client_sock, addr)
    sess.log("connected")
    buf = ""
    try:
        while True:
            try:
                data = client_sock.recv(1024)
            except Exception:
                break
            if not data:
                break
            buf += data.decode("utf-8", errors="ignore")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                sess.handle_line(line)
    except Exception as e:
        sess.log(f"error: {e}")
    finally:
        sess.closing = True
        with sess.lock:
            if sess.tcp_sock:
                try:
                    sess.tcp_sock.close()
                except Exception:
                    pass
        try:
            client_sock.close()
        except Exception:
            pass
        sess.log("disconnected")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080, help="待受ポート(既定 8080)")
    args = ap.parse_args()

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", args.port))
    server.listen(5)
    print(f"[*] Port {args.port} で ESP-AT 相当のテストベッドが待機中...")
    print("    対応: AT / ATE0|1 / AT+RST / AT+GMR / AT+CWMODE= / AT+CWJAP=.. /")
    print("          AT+CIFSR / AT+CIPSTATUS / AT+CIPSTART=\"TCP\",host,port /")
    print("          AT+CIPSEND=<n> / AT+CIPCLOSE")

    try:
        while True:
            client_sock, addr = server.accept()
            threading.Thread(target=handle_client, args=(client_sock, addr), daemon=True).start()
    except KeyboardInterrupt:
        print("\n[*] 終了します。")
        server.close()


if __name__ == "__main__":
    main()
