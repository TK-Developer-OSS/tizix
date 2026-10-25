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
    python3 at_modem.py --serial /dev/ttyUSB0 [--baud 115200]
                                           # 本物の RS-232C の先で WiFi モデム役をする(Raspberry Pi など。pyserial が要る)
    python3 at_modem.py --serial /dev/ttyUSB0 --listen 2323
                                           # さらに TCP 2323 への接続を着信として受ける(RING → ATA / ATS0=n で CONNECT)
"""
import argparse
import re
import socket
import threading
import ssl
import subprocess
import time

FW_VERSION = (
    "AT version:2.4.0.0(at_modem.py test stub)\r\n"
    "SDK version:test\r\n"
    "compile time:2026-09-17\r\n"
    "Bin version:1.0.0(TIZIX-TESTBED)\r\n"
)

CIPSTART_RE = re.compile(
    r'^AT\+CIPSTART="(TCP|UDP|SSL)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*$', re.I)
CIPSEND_RE = re.compile(r'^AT\+CIPSEND=(?:(\d+),)?(\d+)\s*$', re.I)
CIPSERVER_RE = re.compile(r'^AT\+CIPSERVER=(\d+)(?:\s*,\s*(\d+))?', re.I)
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
        self.mux = False                 # AT+CIPMUX=1(複数接続モード)。サーバ(CIPSERVER)の受け側は link id 0 だけ
        self.server = None               # AT+CIPSERVER=1,<port> の待ち受けソケット
        self.data_mode = False           # Hayes: ATDT で CONNECT したあとの生データ中継中(+++ で抜ける)
        self.s0 = 0                      # Hayes S0: 着信を n 回目の RING で自動応答(0 = 自動応答しない、ATA を待つ)
        self.incoming = None             # --listen に来て、まだ応答していない着信のソケット

    def send(self, text):
        if text.strip():
            self.log(f"AT>> {text.strip()!r}")
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
                    self.send(f"\r\n+IPD,0,{len(data)}:" if self.mux else f"\r\n+IPD,{len(data)}:")
                    try:
                        self.client.sendall(data)
                    except Exception:
                        break
            # 相手側が切断した
            if not self.closing:
                with self.lock:
                    if self.tcp_sock is sock:        # こちら(AT+CIPCLOSE)で閉じたあとの二重通知は出さない
                        self.log("target closed connection")
                        self.tcp_sock = None
                        self.send("\r\n0,CLOSED\r\n" if self.mux else "\r\nCLOSED\r\n")
        self.rx_thread = threading.Thread(target=worker, daemon=True)
        self.rx_thread.start()

    # ---- AT コマンド処理 ------------------------------------------------
    def handle_line(self, line):
        line = line.strip()
        if line.startswith("+++"):
            # 相手が切ったあとに送られた切断の合図の残り(ガードタイムの後の "+++" が次の "ATH" と同じ行に来る)。読み飛ばす
            line = line[3:].strip()
        if not line:
            return
        if not line.upper().startswith("AT"):
            # 本物の Hayes モデムは "AT" で始まらない行には何も返さない。以前は ERROR を返していたので、相手がコンソール
            # (シェル)だと、シェルの出力 → ERROR → シェルへの入力 → … の押し付け合いになった(2026-10-25 T105)。
            self.log(f"ignored (not AT): {line[:40]!r}")
            return
        self.log(f"AT<< {line!r}")
        upper = line.upper()

        if upper == "AT":
            self.send("\r\nOK\r\n")
        elif re.match(r"^ATD[TP]?\s*(.+)$", line, re.I):
            self.hayes_dial(re.match(r"^ATD[TP]?\s*(.+)$", line, re.I).group(1).strip())
        elif upper == "ATA":
            self.answer()
        elif re.match(r"^ATS0=\d+$", upper):
            self.s0 = int(upper[5:])
            self.send("\r\nOK\r\n")
        elif upper == "ATH" or upper == "ATH0":
            with self.lock:
                if self.incoming:
                    try:
                        self.incoming.close()
                    except Exception:
                        pass
                    self.incoming = None
                if self.tcp_sock:
                    try:
                        self.tcp_sock.shutdown(socket.SHUT_RDWR)
                    except Exception:
                        pass
                    try:
                        self.tcp_sock.close()
                    except Exception:
                        pass
                    self.tcp_sock = None
            self.send("\r\nOK\r\n")
        elif upper in ("ATZ", "ATQ0", "ATV1", "AT&F") or re.match(r"^AT[A-Z&][0-9]*(=\d+)?$", upper) or upper.startswith("ATS"):
            self.send("\r\nOK\r\n")
        elif upper in ("ATE0", "ATE1"):
            self.send("\r\nOK\r\n")
        elif upper.startswith("AT+CIPMUX="):
            self.mux = upper.endswith("1")
            self.send("\r\nOK\r\n")
        elif CIPSERVER_RE.match(line):
            m = CIPSERVER_RE.match(line)
            self.cipserver(int(m.group(1)), int(m.group(2) or 333))
        elif (upper.startswith("AT+UART_CUR=") or upper.startswith("AT+UART_DEF=")):
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
        elif upper == "AT+CIPSTA?":
            self.send('\r\n+CIPSTA:ip:"192.168.4.2"\r\n+CIPSTA:gateway:"192.168.4.1"\r\n'
                      '+CIPSTA:netmask:"255.255.255.0"\r\n\r\nOK\r\n')
        elif upper == "AT+CIPDNS?":
            self.send('\r\n+CIPDNS:1,"8.8.8.8","8.8.4.4"\r\n\r\nOK\r\n')
        elif upper == "AT+CWSTATE?":
            self.send('\r\n+CWSTATE:2,"testap"\r\n\r\nOK\r\n')
        elif upper == "AT+CWJAP?":
            self.send('\r\n+CWJAP:"testap","aa:bb:cc:00:11:22",6,-50,0,0,0,0,0\r\n\r\nOK\r\n')
        elif upper.startswith("AT+CIPDOMAIN="):
            m = re.match(r'^AT\+CIPDOMAIN="([^"]+)"', line, re.I)
            try:
                ip = socket.gethostbyname(m.group(1))
                self.send(f'\r\n+CIPDOMAIN:"{ip}"\r\n\r\nOK\r\n')
            except Exception:
                self.send("\r\nDNS Fail\r\n\r\nERROR\r\n")
        elif upper.startswith("AT+PING="):
            m = re.match(r'^AT\+PING="([^"]+)"', line, re.I)
            ms = self.do_ping(m.group(1))
            if ms is None:
                self.send("\r\n+PING:TIMEOUT\r\n\r\nERROR\r\n")
            else:
                self.send(f"\r\n+PING:{ms}\r\n\r\nOK\r\n")
        elif upper.startswith("AT+CIPSNTPCFG=") or upper.startswith("AT+CIPSSLCCONF="):
            self.send("\r\nOK\r\n")
        elif upper == "AT+CIPSNTPTIME?":
            t = time.gmtime(time.time() + 9 * 3600)       # tizix は JST 加算済みの値を「そのまま表示」する約束
            self.send("\r\n+CIPSNTPTIME:" + time.strftime("%a %b %d %H:%M:%S %Y", t) + "\r\n\r\nOK\r\n")
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
                    if proto.upper() == "SSL":
                        ctx = ssl.create_default_context()
                        ctx.check_hostname = False
                        ctx.verify_mode = ssl.CERT_NONE       # テストベッド: 自己署名でも通す
                        s = ctx.wrap_socket(s, server_hostname=host)
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
            n = int(m.group(2))
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
        elif upper == "AT+CIPCLOSE" or upper.startswith("AT+CIPCLOSE="):
            with self.lock:
                if self.tcp_sock:
                    try:
                        self.tcp_sock.shutdown(socket.SHUT_RDWR)   # 受信スレッドが recv 中でも FIN を出す
                    except Exception:
                        pass
                    try:
                        self.tcp_sock.close()
                    except Exception:
                        pass
                    self.tcp_sock = None
                    self.send("\r\n0,CLOSED\r\n\r\nOK\r\n" if self.mux else "\r\nCLOSED\r\n\r\nOK\r\n")
                else:
                    self.send("\r\nERROR\r\nNOT CONNECTED\r\n")
        else:
            self.log(f"unrecognized command: {line!r}")
            self.send("\r\nERROR\r\n")

    def cipserver(self, mode, port):
        """AT+CIPSERVER=<mode>[,<port>]: mode 1 でホスト側に待ち受けソケットを作る(127.0.0.1)。
        受けた接続は link id 0 として扱い、"0,CONNECT" を通知する。受けている間に来た接続は OS のバックログで待たせる。"""
        if mode == 0:
            if self.server:
                try:
                    self.server.close()
                except Exception:
                    pass
                self.server = None
            self.send("\r\nOK\r\n")
            return
        if not self.mux:
            self.send("\r\nERROR\r\n")          # 実機の ESP-AT も CIPMUX=1 が必要
            return
        if self.server:
            self.send("\r\nOK\r\n")
            return
        try:
            srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind(("127.0.0.1", port))
            srv.listen(4)
        except Exception as e:
            self.log(f"CIPSERVER failed: {e}")
            self.send("\r\nERROR\r\n")
            return
        self.server = srv
        self.log(f"CIPSERVER listening on 127.0.0.1:{port}")

        def acceptor():
            while self.server is srv and not self.closing:
                try:
                    c, _ = srv.accept()
                except Exception:
                    break
                # link 0 が空くまで待つ(使用中なら順番待ち)
                while not self.closing and self.server is srv:
                    with self.lock:
                        if self.tcp_sock is None:
                            self.tcp_sock = c
                            self.send("\r\n0,CONNECT\r\n")
                            break
                    time.sleep(0.05)
                else:
                    try:
                        c.close()
                    except Exception:
                        pass
                    break
                self.start_rx_forwarder()
        threading.Thread(target=acceptor, daemon=True).start()
        self.send("\r\nOK\r\n")

    def hayes_dial(self, target):
        """ATDT host:port(WiFi モデムの流儀)。つながったら "CONNECT 9600" のあと data_mode(生データ中継)へ。
        port 37 は RFC 868 の time(1900 年からの秒数 4 バイト)をこちらで返してすぐ切る(時計合わせの試験用。外へ出ない)。"""
        m = re.match(r"^(.+?)[:,](\d+)$", target)
        if not m:
            self.send("\r\nNO CARRIER\r\n")
            return
        host, port = m.group(1), int(m.group(2))
        if port == 37:
            now = int(time.time()) + 2208988800
            self.send("\r\nCONNECT 9600\r\n")
            try:
                self.client.sendall(now.to_bytes(4, "big"))
            except Exception:
                pass
            time.sleep(0.2)
            self.send("\r\nNO CARRIER\r\n")
            return
        with self.lock:
            if self.tcp_sock:
                self.send("\r\nBUSY\r\n")
                return
        self.log(f"ATDT -> connecting to {host}:{port}")
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(10)
            s.connect((host, port))
            s.settimeout(None)
        except Exception as e:
            self.log(f"ATDT failed: {e}")
            self.send("\r\nNO CARRIER\r\n")
            return
        with self.lock:
            self.tcp_sock = s
            self.data_mode = True
        self.send("\r\nCONNECT 9600\r\n")
        self.start_raw_forwarder()

    def answer(self):
        """ATA(または S0 の自動応答): 鳴っている着信に出て CONNECT、データモードへ。"""
        with self.lock:
            sock = self.incoming
            self.incoming = None
            if sock is None or self.tcp_sock:
                if sock is not None:
                    try:
                        sock.close()
                    except Exception:
                        pass
                self.send("\r\nNO CARRIER\r\n")
                return
            self.tcp_sock = sock
            self.data_mode = True
        self.log("answered an incoming call")
        self.send("\r\nCONNECT 9600\r\n")
        self.start_raw_forwarder()

    def ring(self, conn):
        """着信: 2 秒ごとに RING。S0 回目で自動応答、ATA が来ればそこで終わり、10 回で諦めて切る。"""
        for i in range(10):
            if self.incoming is not conn or self.closing:
                return
            self.send("\r\nRING\r\n")
            if self.s0 and i + 1 >= self.s0:
                self.answer()
                return
            time.sleep(2)
        with self.lock:
            if self.incoming is conn:
                self.incoming = None
                try:
                    conn.close()
                except Exception:
                    pass
        self.log("incoming call not answered")

    def start_raw_forwarder(self):
        """data_mode 中: 相手 → 端末の生バイト中継。相手が切ったら NO CARRIER を出してコマンドモードへ戻る。"""
        def worker():
            sock = self.tcp_sock
            while not self.closing and sock is not None:
                try:
                    data = sock.recv(4096)
                except Exception:
                    break
                if not data:
                    break
                with self.lock:
                    try:
                        self.client.sendall(data)
                    except Exception:
                        break
            if not self.closing:
                with self.lock:
                    if self.tcp_sock is sock:
                        self.tcp_sock = None
                        self.data_mode = False
                        self.send("\r\nNO CARRIER\r\n")
        threading.Thread(target=worker, daemon=True).start()

    def do_ping(self, host):
        """ホストの ping(ICMP)。ping コマンドが無い/失敗なら None。ms の整数を返す。"""
        try:
            r = subprocess.run(["ping", "-c", "1", "-W", "2", host], capture_output=True, text=True, timeout=5)
            m = re.search(r"time[=<]([0-9.]+)", r.stdout)
            if r.returncode == 0 and m:
                return max(1, int(float(m.group(1)) + 0.5))
        except Exception:
            pass
        return None

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


CURRENT = [None]                         # --listen の着信を渡す先(今のセッション)


def listener(port):
    """--listen: 外からの TCP 接続を「着信」として今のセッションへ回す(RING → ATA / S0 で CONNECT)。話し中なら断る。"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(2)
    print(f"[*] 着信を TCP {port} で受ける")
    while True:
        conn, addr = srv.accept()
        sess = CURRENT[0]
        if sess is None or sess.tcp_sock or sess.incoming:
            try:
                conn.sendall(b"BUSY\r\n")
                conn.close()
            except Exception:
                pass
            continue
        sess.log(f"incoming call from {addr[0]}:{addr[1]}")
        sess.incoming = conn
        threading.Thread(target=sess.ring, args=(conn,), daemon=True).start()


def handle_client(client_sock, addr):
    sess = Session(client_sock, addr)
    CURRENT[0] = sess
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
            if sess.data_mode:
                if data.strip() == b"+++":                 # エスケープ(前後のガードタイムはテストベッドでは見ない)
                    sess.data_mode = False
                    sess.send("\r\nOK\r\n")
                else:
                    with sess.lock:
                        try:
                            if sess.tcp_sock:
                                sess.tcp_sock.sendall(data)
                        except Exception:
                            pass
                continue
            buf += data.decode("utf-8", errors="ignore")
            buf = buf.replace("\r\n", "\n").replace("\r", "\n")     # Hayes は CR だけで行を終える
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                sess.handle_line(line)
    except Exception as e:
        sess.log(f"error: {e}")
    finally:
        sess.closing = True
        if sess.server:
            try:
                sess.server.close()
            except Exception:
                pass
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


class SerialSock:
    """pyserial のポートを、Session が使うソケットの形(recv / sendall / close)で見せる。"""

    def __init__(self, dev, baud):
        import serial                    # --serial のときだけ要る
        self.port = serial.Serial(dev, baud, bytesize=8, parity="N", stopbits=1, timeout=None,
                                  rtscts=False, dsrdtr=False, xonxoff=False)
        self.wlock = threading.Lock()

    def recv(self, n):
        first = self.port.read(1)        # 1 バイト来るまで待つ
        time.sleep(0.02)                 # 続けて来る分をまとめる("+++" を 1 回の recv で受けるため)
        more = self.port.in_waiting
        if more and n > 1:
            first += self.port.read(min(more, n - 1))
        return first

    def sendall(self, data):
        with self.wlock:
            self.port.write(data)
            self.port.flush()

    def close(self):
        pass                             # 回線は開けたまま(次の呼び出しもこの線で受ける)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080, help="待受ポート(既定 8080)")
    ap.add_argument("--serial", help="TCP ではなくこのシリアル(例: /dev/ttyUSB0)の先でモデム役をする")
    ap.add_argument("--baud", type=int, default=115200, help="--serial の速さ(既定 115200、8N1、フロー制御なし)")
    ap.add_argument("--listen", type=int, help="この TCP ポートへの接続を着信(RING)として受ける(ATA か ATS0=n で応答)")
    args = ap.parse_args()
    if args.listen:
        threading.Thread(target=listener, args=(args.listen,), daemon=True).start()

    if args.serial:
        line = SerialSock(args.serial, args.baud)
        print(f"[*] {args.serial} ({args.baud}bps 8N1) でモデム役として待機中(Hayes ATDT host:port / ESP-AT)")
        try:
            while True:                  # 回線は 1 本。セッションが終わっても同じ線で次を受ける
                handle_client(line, args.serial)
        except KeyboardInterrupt:
            print("\n[*] 終了します。")
        return

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
