#!/usr/bin/env python3
"""test_esp_net.py - #62 z80board(ESP-WROOM-02 / SPI bit-bang)の WiFi 経路を
z80boardsim 上で通しで検証する。

  構成:
      tizix (z80boardsim)
        └ port 0x81 = bit-bang SPI ── iosim.c の ESP スレーブ模擬
              └ TCP ─→ at_modem.py(ESP-AT テストベッド、:8080)
                          └ 実 TCP ─→ このスクリプトの待受(:9100)

  実機では「iosim.c の模擬 + at_modem.py」の位置に ESP-WROOM-02
  (arch/z80board/esp/tzesp_at)が入る。Z80 側(user/netesp.c)のコードは
  どちらでも同じ。

  検証項目:
    1. `net &`        … ESP とのリンク確立(AT / ATE0 / AT+CIPMUX=0)
    2. `wifi AT+GMR`  … NETCMD_ATCMD の AT パススルーと応答の読み出し
    3. `telnet 127.0.0.1 9100`
                      … AT+CIPSTART → +IPD 受信 → AT+CIPSEND 送信
  使い方: python3 python/test_esp_net.py
"""
import os
import pty
import select
import socket
import subprocess
import sys
import threading
import time

TIZIX = os.environ.get("TIZIX_ROOT", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARCH_DIR = os.path.join(TIZIX, "arch", "z80board")
SIM_CMD = ["./z80boardsim", "-x", "../../build/arch/z80board/obj/kernel.ihx"]

MODEM_PORT = 8080
TARGET_PORT = 9100
PONG = b"PONG-FROM-HOST\n"

got_from_guest = []


def target_server(stop):
    """at_modem が CIPSTART で繋いでくる相手。繋がったら PONG を送り、
    以後ゲストから届いたバイト列を got_from_guest へ溜める。"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", TARGET_PORT))
    srv.listen(4)
    srv.settimeout(0.5)
    while not stop.is_set():
        try:
            cs, _ = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        print(f"[target] 接続あり → {PONG!r} を送る")
        try:
            cs.sendall(PONG)
        except OSError:
            pass
        cs.settimeout(0.5)
        while not stop.is_set():
            try:
                d = cs.recv(256)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d:
                break
            print(f"[target] ゲストから受信: {d!r}")
            got_from_guest.append(d)
        cs.close()
    srv.close()


def main():
    stop = threading.Event()
    t = threading.Thread(target=target_server, args=(stop,), daemon=True)
    t.start()

    modem = subprocess.Popen(
        [sys.executable, os.path.join(TIZIX, "python", "at_modem.py"),
         "--port", str(MODEM_PORT)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    env = dict(os.environ)
    env["TZESP_MODEM"] = f"127.0.0.1:{MODEM_PORT}"

    master, slave = pty.openpty()
    proc = subprocess.Popen(SIM_CMD, cwd=ARCH_DIR, env=env,
                            stdin=slave, stdout=slave, stderr=slave,
                            close_fds=True)
    os.close(slave)

    buf_all = bytearray()

    def pump(pattern, timeout):
        local = bytearray()
        t0 = time.time()
        while time.time() - t0 < timeout:
            r, _, _ = select.select([master], [], [], 0.2)
            if r:
                try:
                    c = os.read(master, 4096)
                except OSError:
                    return bytes(local), False
                if not c:
                    return bytes(local), False
                local += c
                buf_all.extend(c)
                sys.stdout.write(c.decode("latin-1", "replace"))
                sys.stdout.flush()
                if pattern and pattern in local:
                    return bytes(local), True
        return bytes(local), False

    def seen(pat):
        """改行を無視して全出力から照合する。telnet は接続後、コマンド行に
        残った改行を「入力」として読み、相手へ送りつつローカルにもエコーする
        ── 受信文字列の途中に改行が 1 個挟まって見えるのはその副作用で、
        ゲスト側のバグではない(2026-09-21 に FAIL 3 として一度踏んだ)。"""
        flat = bytes(buf_all).replace(b"\r", b"").replace(b"\n", b"")
        return pat.replace(b"\r", b"").replace(b"\n", b"") in flat

    results = {}
    try:
        # プロンプト "[/root]# " を待つ。b"# " だと z80boardsim の起動バナー
        # (#### のアスキーアート)に一致し、シェルが立つ前に打ってしまう。
        pump(b"]# ", 40.0)

        print("\n--- net & ---")
        os.write(master, b"net &\r")
        pump(b"ESP link ok", 25.0)
        results["1. net & で ESP とリンク"] = seen(b"ESP link ok")

        print("\n--- wifi AT+GMR ---")
        os.write(master, b"wifi AT+GMR\r")
        pump(b"TIZIX-TESTBED", 25.0)
        results["2. wifi の AT パススルー"] = seen(b"TIZIX-TESTBED")

        print("\n--- telnet 127.0.0.1 9100 ---")
        os.write(master, b"telnet 127.0.0.1 9100\r")
        pump(b"PONG-FROM-HOST", 40.0)
        results["3. +IPD 受信(PONG)"] = seen(b"PONG-FROM-HOST")

        os.write(master, b"HELLO-FROM-TIZIX\r")
        t0 = time.time()
        while time.time() - t0 < 20.0:
            pump(None, 0.5)
            # 受信は連結して見る。net は TX リングにその時あった分だけを 1 回の
            # AT+CIPSEND で送るので、負荷によっては 2 回に分かれて届く(回帰の中で
            # それで FAIL した。単独では 1 回で届くことが多い)。
            if b"HELLO-FROM-TIZIX" in b"".join(got_from_guest):
                break
        results["4. AT+CIPSEND 送信"] = (
            b"HELLO-FROM-TIZIX" in b"".join(got_from_guest))

        os.write(master, b"\x03")
        pump(b"# ", 10.0)
    finally:
        try:
            proc.terminate()
        except Exception:
            pass
        stop.set()
        try:
            modem.terminate()
        except Exception:
            pass

    print("\n================ 結果 ================")
    allok = True
    for k, v in results.items():
        print(f"  {'PASS' if v else 'FAIL'}  {k}")
        allok = allok and v
    print("======================================")
    print("*** ALL PASS ***" if allok else "*** FAIL あり ***")
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())
