#!/usr/bin/env python3
"""test_net.py - x86-i386-unreal のネットワーク(ESP-AT over 2 番目のシリアル)の QEMU 試験。

  構成: ゲストの COM2(0x2F8)= QEMU の `-serial tcp:127.0.0.1:8080` → python/at_modem.py(ESP-AT エミュレータ)
        → ホストの簡易 HTTP サーバ(127.0.0.1:8765)。ゲスト側は netcli16.c(telnet / curl)。
  実行: リポジトリのルートで  python3 arch/x86-i386-unreal/tools/test_net.py   (先に make ARCH=x86-i386-unreal all)
  注意: 以前は「複数文字をまとめて送ると最後の 1 文字が次の入力まで届かない」現象があり QEMU の 16550 の癖と見ていたが、
        実際は kbhit(syscall 17)が io.c の戻しバッファ(con_break が Ctrl+C の確認で取り出した 1 文字)を見ていない
        カーネルのバグだった(2026-10-23 修正済み)。今は普通に連続入力できるが、試験は念のため 1 文字ずつ送る。"""
import os, pty, select, subprocess, sys, tempfile, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
IMG = os.environ.get("TIZIX_IMG") or os.path.join(ROOT, "arch/x86-i386-unreal/disk/floppy.img")
MARK = "hello-from-host-tizix-net"
# 試験の対象: NET=modem(既定。Hayes モデム = ATDT)/ NET=esp(ESP-AT)。ビルド時の NET と同じ値を指定する。
NETMODE = os.environ.get("NET", "modem")

def main():
    if not os.path.exists(IMG):
        print("floppy.img が無い。先に make ARCH=x86-i386-unreal all"); return 2
    import socket
    for port in (8080, 8765, 8766, 9000, 8000):          # 前回の at_modem / http.server が残っていると試験が不安定になる
        t = socket.socket()
        t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)    # 前回の TIME_WAIT は許す(待ち受け中のものだけ弾く)
        try:
            t.bind(("127.0.0.1", port))
        except OSError:
            print("ポート %d が使用中(前回の at_modem.py / http.server / qemu が残っていないか確認)" % port)
            return 2
        finally:
            t.close()
    www = tempfile.mkdtemp()
    open(os.path.join(www, "index.html"), "w").write("<html>%s</html>\n" % MARK)
    atlog = open("/tmp/at_modem_test.log", "w") if os.environ.get("NETTEST_DEBUG") else subprocess.DEVNULL
    at = subprocess.Popen([sys.executable, "-u", os.path.join(ROOT, "python/at_modem.py"), "--port", "8080"],
                          stdout=atlog, stderr=subprocess.STDOUT)
    hs = subprocess.Popen([sys.executable, "-m", "http.server", "8765", "--bind", "127.0.0.1"], cwd=www,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # TLS(curl https)用: 自己署名の証明書で簡易 HTTPS サーバ(8766)
    tls = None
    if subprocess.call(["which", "openssl"], stdout=subprocess.DEVNULL) == 0:
        crt, key = os.path.join(www, "c.pem"), os.path.join(www, "k.pem")
        subprocess.call(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", crt,
                         "-subj", "/CN=localhost", "-days", "1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        srv = ("import http.server,ssl\n"
               "h=http.server.HTTPServer(('127.0.0.1',8766),http.server.SimpleHTTPRequestHandler)\n"
               "c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);c.load_cert_chain(%r,%r)\n"
               "h.socket=c.wrap_socket(h.socket,server_side=True);h.serve_forever()\n" % (crt, key))
        tls = subprocess.Popen([sys.executable, "-c", srv], cwd=www, stdout=subprocess.DEVNULL,
                               stderr=open("/tmp/tls_test.log", "w") if os.environ.get("NETTEST_DEBUG") else subprocess.DEVNULL)
    time.sleep(1.5)
    for port in ((8765, 8766) if tls else (8765,)):       # サーバが待ち受けを始めるまで待つ(鍵の生成に数秒かかることがある)
        for _ in range(60):
            try:
                socket.create_connection(("127.0.0.1", port), timeout=1).close()
                break
            except OSError:
                time.sleep(0.5)
    m, s = pty.openpty()
    q = subprocess.Popen(["qemu-system-i386", "-m", "128", "-drive", "file=%s,format=raw,if=floppy" % IMG,
                          "-chardev", "stdio,id=c0,signal=off", "-serial", "chardev:c0",
                          "-serial", "tcp:127.0.0.1:8080", "-display", "none", "-no-reboot", "-boot", "a"],
                         stdin=s, stdout=s, stderr=s)
    buf = b""
    def rd(t):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([m], [], [], 0.1)
            if r: buf += os.read(m, 65536)
    def send(c, t):
        os.write(m, c.encode()); rd(t)
    fails = 0
    try:
        rd(5)
        rd(10)      # 起動時に /etc/rc の `ntpdate &` が COM2 を使う(終わるまで待つ。モデムは同時に 1 つのプロセスしか使えない)
        # 2) telnet: 対話(短く区切って入力)
        n = len(buf)
        send("telnet 127.0.0.1:8765\r", 8)
        for ch in "GET / HTTP/1.0\r\r":
            send(ch, 0.6)
        rd(5)
        os.write(m, b"\x03"); rd(3)
        out = buf[n:].decode(errors="replace")
        if os.environ.get("NETTEST_DEBUG"): print(out[-700:])
        ok = "connected" in out and "200 OK" in out and MARK in out
        print("%-44s | %s" % ("telnet 127.0.0.1:8765 (GET)", "PASS" if ok else "FAIL")); fails += not ok
        # 2b) 共通コマンド(gcc 系と同じ API)。1 コマンドずつ、入力は 1 文字ずつ(上の注意)
        def run(cmd, wait):
            nonlocal buf
            n = len(buf)
            for ch in cmd + "\r":
                send(ch, 0.15)
            rd(wait)
            return buf[n:].decode(errors="replace")
        def check(name, out, *needles):
            nonlocal fails
            ok = all(x in out for x in needles)
            print("%-44s | %s" % (name, "PASS" if ok else "FAIL")); fails += not ok
            if not ok and os.environ.get("NETTEST_DEBUG"): print(out[-600:])
        esp = NETMODE == "esp"
        if esp:
            check("ifconfig", run("ifconfig", 8), "inet 192.168.4.2", "netmask 255.255.255.0", "ether aa:bb:cc:dd:ee:ff")
            check("dig localhost", run("dig localhost", 8), "IN\tA\t127.0.0.1")
            check("ping -c 2 127.0.0.1", run("ping -c 2 127.0.0.1", 12), "2 packets transmitted, 2 packets received")
        else:   # モデムには IP のインタフェース・DNS 照会・ICMP は無い(失敗を返すのが正しい)
            check("ifconfig (no interface)", run("ifconfig", 8), "no network interface")
            check("dig (modem: no answer)", run("dig localhost", 8), "no answer")
        check("curl http://127.0.0.1:8765/", run("curl http://127.0.0.1:8765/", 10), MARK)
        if esp and tls:
            check("curl -k https://127.0.0.1:8766/", run("curl -k https://127.0.0.1:8766/", 25), MARK)
        check("ntpdate (clock set)", run("ntpdate", 10), "clock set")
        check("date (after ntpdate)", run("date", 4), "2026-")
        if esp:
            check("wifi (AT+CIFSR)", run("wifi", 6), "+CIFSR:STAIP")
            check("wifi join testap secret", run("wifi join testap secret", 8), "WIFI GOT IP")
        else:
            check("wifi ATI (AT passthrough)", run("wifi ATI", 6), "OK")
        if esp:
            check("netstat", run("netstat", 6), "Proto")
            check("arp", run("arp", 6), "Address")
        else:
            check("netstat (no interface)", run("netstat", 6), "no network interface")
            check("arp (no interface)", run("arp", 6), "no network interface")
        # nc: こちらから繋ぐ(HTTP/1.0 の GET を 1 文字ずつ)
        n = len(buf)
        send("nc 127.0.0.1 8765\r", 8)
        for ch in "GET / HTTP/1.0\r\r":
            send(ch, 0.6)
        rd(5); os.write(m, b"\x03"); rd(3)
        out = buf[n:].decode(errors="replace")
        check("nc 127.0.0.1 8765 (GET)", out, "200 OK", MARK)
        # 3) 接続できない先
        n = len(buf)
        out = run("curl http://127.0.0.1:1/", 12)
        ok = "cannot connect" in out
        print("%-44s | %s" % ("curl 127.0.0.1:1 (cannot connect)", "PASS" if ok else "FAIL")); fails += not ok
        if NETMODE == "esp":
            # 4) 待ち受け: nc -l(ホストから at_modem の CIPSERVER 経由で接続)
            n = len(buf)
            for ch in "nc -l 9000\r":
                send(ch, 0.15)
            rd(8)
            import socket
            try:
                c = socket.create_connection(("127.0.0.1", 9000), timeout=5)
                c.sendall(b"hello-host\n"); rd(4)
                for ch in "reply\r":
                    send(ch, 0.3)
                rd(3)
                c.settimeout(3)
                got = c.recv(100)
                c.close()
            except Exception as e:
                got = b"[%s]" % str(e).encode()
            rd(4)
            out = buf[n:].decode(errors="replace")
            check("nc -l 9000 (host -> guest)", out, "hello-host")
            check("nc -l 9000 (guest -> host)", got.decode(errors="replace"), "reply")
            # 5) MCP サーバ(mcpd)。tools/list・run・write_file/read_file・list_dir
            import json
            def rpc(obj):
                body = json.dumps(obj).encode()
                c = socket.create_connection(("127.0.0.1", 8000), timeout=25)
                c.sendall(b"POST /mcp HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nAccept: application/json\r\n"
                          b"Content-Length: " + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body)
                data = b""
                try:
                    while True:
                        d = c.recv(4096)
                        if not d: break
                        data += d
                except Exception as e:
                    data += b"[recv err %s]" % str(e).encode()
                c.close()
                return data.decode(errors="replace")
            for ch in "mcpd 8000 &\r":
                send(ch, 0.15)
            rd(10)
            check("mcpd tools/list", rpc({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}), '"name":"run"', "read_file")
            rd(2)
            check("mcpd run echo", rpc({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "run", "arguments": {"command": "echo hello-from-mcp"}}}), "hello-from-mcp", "[exit 0]")
            rd(2)
            check("mcpd write_file/read_file", rpc({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "write_file", "arguments": {"path": "/root/mcp.txt", "content": "mcp-file-body"}}}), "wrote")
            rd(2)
            check("mcpd read_file", rpc({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "read_file", "arguments": {"path": "/root/mcp.txt"}}}), "mcp-file-body")
            rd(2)
            check("mcpd list_dir /", rpc({"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "list_dir", "arguments": {"path": "/"}}}), "bin/")
    finally:
        for p in (q, at, hs, tls):
            if p: p.kill()
    print("=" * 60); print("FAIL %d" % fails)
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
