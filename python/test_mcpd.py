#!/usr/bin/env python3
# test_mcpd.py - MCP サーバ mcpd(#110)。esp32 の実機(TIZIX_HW)だけ。QEMU は WiFi が無いので SKIP。
#
#   使い方: TIZIX_ARCH=esp32-wroom-32e TIZIX_HW=47001 python3 python/test_mcpd.py
#   実機で `mcpd &` を起こし、ifconfig で IP を見て、rocky9 から HTTP の POST で JSON-RPC を投げる:
#     initialize / notifications/initialized(202)/ tools/list / tools/call(run・list_dir・write_file・
#     read_file・esp32_device)/ 知らないメソッド / GET(405)
import os
import re
import sys
import pty
import json
import time
import select
import subprocess
import urllib.request
import urllib.error

os.environ["TIZIX_ARCH"] = sys.argv[1] if len(sys.argv) > 1 else "esp32-wroom-32e"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tzpaths

if not os.environ.get("TIZIX_HW"):
    print("SKIP: mcpd needs WiFi (real hardware, TIZIX_HW)")
    print("RESULT: PASS")
    sys.exit(0)

master, slave = pty.openpty()
proc = subprocess.Popen(
    tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD,
    stdin=slave, stdout=slave, stderr=slave, close_fds=True,
)
os.close(slave)


def read_until(pat, timeout=8.0):
    buf = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.1)
        if master in r:
            try:
                c = os.read(master, 4096)
            except OSError:
                break
            if not c:
                break
            buf += c
            if pat.encode() in buf:
                break
    return buf.decode("latin-1", "replace")


def run(cmd, timeout=10.0):
    os.write(master, (cmd + "\r").encode())
    out = read_until("]# ", timeout).replace("\r", "")
    print("$ " + cmd + "\n" + out)
    return out


fails = 0


def check(name, cond, detail=""):
    global fails
    print(("[PASS] " if cond else "[FAIL] ") + name + ("" if cond else "  " + repr(detail)[:600]))
    if not cond:
        fails += 1


boot = read_until("]# ", 30.0)
check("boot prompt", boot.endswith("]# "), boot[-80:])
ip = None
for _ in range(20):                                   # DHCP は 10 秒ほど
    m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", run("ifconfig"))
    if m and m.group(1) != "0.0.0.0":
        ip = m.group(1)
        break
    time.sleep(2)
check("WiFi の IP", ip is not None)
if not ip:
    proc.kill()
    print("RESULT: FAIL")
    sys.exit(1)

run("mcpd &")
time.sleep(2)
URL = "http://%s:8000/mcp" % ip
nid = [0]


def post(obj, expect_body=True):
    data = json.dumps(obj).encode()
    for attempt in range(3):
        try:
            r = urllib.request.urlopen(urllib.request.Request(
                URL, data=data, headers={"Content-Type": "application/json",
                                         "Accept": "application/json, text/event-stream"}), timeout=40)
            body = r.read().decode("utf-8", "replace")
            print(">>", json.dumps(obj)[:200], "\n<<", r.status, body[:400])
            return r.status, (json.loads(body) if expect_body and body else None)
        except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
            print("retry", attempt, e)
            time.sleep(1)
    return None, None


def call(method, params=None):
    nid[0] += 1
    return post({"jsonrpc": "2.0", "id": nid[0], "method": method, "params": params or {}})


def tool(name, args):
    st, js = call("tools/call", {"name": name, "arguments": args})
    if not js or "result" not in js:
        return None, js
    return js["result"]["content"][0]["text"], js["result"]


st, js = call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                             "clientInfo": {"name": "test_mcpd", "version": "1"}})
check("initialize", st == 200 and js and js["result"]["serverInfo"]["name"] == "tizix-mcpd"
      and js["result"]["protocolVersion"] == "2025-06-18", js)
st, _ = post({"jsonrpc": "2.0", "method": "notifications/initialized"}, expect_body=False)
check("通知は 202", st == 202, st)
st, js = call("tools/list")
names = [t["name"] for t in js["result"]["tools"]] if js else []
check("tools/list", set(names) == {"run", "read_file", "write_file", "list_dir", "esp32_device"}, names)

text, res = tool("run", {"command": "ls /"})
check("run ls /", text is not None and "bin/" in text and "[exit 0]" in text, res)
text, res = tool("run", {"command": "mbtest"})
check("run の終了コード(mbtest は 1)", text is not None and "[exit 1]" in text and res.get("isError"), res)
text, res = tool("list_dir", {"path": "/etc"})
check("list_dir /etc", text is not None and "rc" in text, res)
text, res = tool("write_file", {"path": "/mcp-test.txt", "content": "hello \"mcp\"\nline2\n"})
check("write_file", text is not None and "wrote 18 bytes" in text, res)
text, res = tool("read_file", {"path": "/mcp-test.txt"})
check("read_file で書いたものが読める", text == 'hello "mcp"\nline2\n', res)
tool("run", {"command": "rm /mcp-test.txt"})
text, res = tool("esp32_device", {"request": "gpio 6 1"})
check("esp32_device(拒否の文言が esp32d から届く)", text is not None and "refused" in text and res.get("isError"), res)
text, res = tool("esp32_device", {"request": "rgb green"})
check("esp32_device rgb green", text is not None and not res.get("isError"), res)
tool("esp32_device", {"request": "rgb off"})
st, js = call("no/such")
check("知らないメソッドは -32601", js and js.get("error", {}).get("code") == -32601, js)
try:
    urllib.request.urlopen(URL, timeout=10)
    check("GET は 405", False)
except urllib.error.HTTPError as e:
    check("GET は 405", e.code == 405, e.code)

out = run("ps")
m = re.search(r"^(\d+) \S+ mcpd", out, re.M)
if m:
    run("kill " + m.group(1))
proc.kill()
print("RESULT: " + ("PASS" if fails == 0 else "FAIL (%d)" % fails))
sys.exit(1 if fails else 0)
