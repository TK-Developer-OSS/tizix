#!/usr/bin/env python3
"""winerun.py - PC-8801 の回帰を、rocky9 の podman コンテナ(Wine + Xvfb)上の M88 で回す。

  ホストのキーボード・マウス・画面に一切触れない(Windows 版ハーネスは廃止)。
  コンテナ wine88(Ubuntu 24.04 + wine + xvfb + xdotool + imagemagick)は ~/tmp/wine_setup.sh で作る。
  ~/work/pc88 がコンテナの /pc88。M88.exe / N88.ROM / DISK.ROM / FONT.ROM / M88.ini を置いておく。

  使い方:
    winerun.py boot                       M88 を起動して FAT 検出まで待つ(既存は止める)
    winerun.py run "date" "ls /"          起動済みの M88 へコマンドを打ち、画面の文字列を出す
    winerun.py tests pc88_tests.txt       テストファイルで PASS/FAIL(終了コード 0/1)
    winerun.py shot out.png               画面を PNG に保存

  画面の文字列は、M88 のプロセスメモリ(/proc/<pid>/mem)からテキスト VRAM を読む(読み取りのみ)。
"""
import os, re, subprocess, sys, time

C = "wine88"
D = os.environ.get("WINERUN_DISPLAY", ":99")   # ":0" などにすると実画面(フォアグラウンド)で動かす
WORK = os.path.expanduser("~/work/pc88")
SHOTS = os.path.expanduser("~/tizix/test/build/pc88_shots")   # SMB の作業ディレクトリ内(読み取りに確認が出ない)
ARCH = os.path.expanduser("~/tizix/test/arch/z80-pc8801/disks")
ROW0, PITCH = 0, 120


NATIVE = not subprocess.run(["which", "podman"], capture_output=True).returncode == 0   # podman が無ければホストの wine を直接使う


def pe(cmd, detach=False, check=False):
    if NATIVE:
        env = dict(os.environ, DISPLAY=D)
        cmd = cmd.replace("/pc88", WORK)
        if detach:
            subprocess.Popen(["sh", "-c", cmd], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=True)
            return subprocess.CompletedProcess(cmd, 0, "", "")
        return subprocess.run(["sh", "-c", cmd], env=env, capture_output=True, text=True, check=check)
    a = ["podman", "exec"] + (["-d"] if detach else []) + ["-e", "DISPLAY=" + D, C, "sh", "-c", cmd]
    return subprocess.run(a, capture_output=True, text=True, check=check)


def m88_pid():
    r = subprocess.run(["pgrep", "-f", r"M88\.exe"], capture_output=True, text=True)
    for p in r.stdout.split():
        try:
            # argv[0] が M88.exe のもの(sh -c "... wine M88.exe" のラッパーや、刈り取られないゾンビを除く)
            argv0 = open("/proc/%s/cmdline" % p, "rb").read().split(b"\0")[0].decode("latin-1")
            if argv0.replace("\\", "/").split("/")[-1].strip() == "M88.exe":
                m = open("/proc/%s/maps" % p).read()
                if len(m) > 1000:
                    return int(p)
        except OSError:
            pass
    return None


def boot(wait=240):
    # コマンドライン引数でディスクを渡すと、リセット後にブートしない(Windows 版で確認済み)。
    # 引数なしで起動し、Disk メニューから 2 枚とも入れてから F12(リセット)。
    pe("pkill -f M88.exe; wineserver -k; true")
    time.sleep(4)
    _screen.clear()
    for n in ("tizix_a.d88", "tizix_b.d88"):
        src = os.path.join(ARCH, "drive%s.d88" % ("a" if n.endswith("a.d88") else "b"))
        subprocess.run(["cp", "-f", src, os.path.join(WORK, n)], check=True)
    if D == ":99" and pe("pgrep Xvfb").returncode != 0:
        # コンテナを強制終了すると古いロックが残り、Xvfb が "Server is already active" で起動しない
        pe("rm -f /tmp/.X99-lock /tmp/.X11-unix/X99")
        pe("Xvfb %s -screen 0 1024x768x24 >/tmp/xvfb.log 2>&1" % D, detach=True)
        time.sleep(2)
    pe("cd /pc88 && WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml= wine M88.exe >/tmp/wine.log 2>&1", detach=True)
    t0 = time.time()
    while not wid():
        if time.time() - t0 > 180:
            log("M88 window not found: %r" % pe("xdotool search --name . getwindowname %@").stdout)
            return False
        time.sleep(1)
    time.sleep(8)
    shot(os.path.join(SHOTS, "boot0.png"))
    if not set_drive(1, "tizix_a.d88") or not set_drive(2, "tizix_b.d88"):
        return False
    shot(os.path.join(SHOTS, "boot1.png"))
    focus()
    key("F12")
    t0 = time.time()
    while time.time() - t0 < wait:
        time.sleep(3)
        if find_screen() is not None:
            return True
    shot(os.path.join(SHOTS, "boot2.png"))
    return False


def log(s):
    print("[winerun] " + s, flush=True)


# ウィンドウマネージャがある実画面では、枠ウィンドウ(mutter-x11-frames)も同じ名前で検索に掛かるので除く
_NOFRAME = "| while read w; do xprop -id $w WM_CLASS 2>/dev/null | grep -q -e mutter-x11-frames -e '\"xdotool\"' || echo $w; done | head -1"


def wid():
    r = pe("xdotool search --class '^m88.exe$' %s" % _NOFRAME)
    return r.stdout.strip()


def dlg():
    return pe("xdotool search --onlyvisible --name '^Open disk image' %s" % _NOFRAME).stdout.strip()


def focus():
    w = wid()
    if w:
        pe("xdotool windowactivate --sync %s 2>/dev/null || xdotool windowfocus --sync %s; true" % (w, w))   # WM があれば前面化(Xvfb は windowfocus)
        time.sleep(0.3)
    return w


def set_drive(n, fname):
    """Disk メニュー → Drive n → ファイル名(M88 のカレント /pc88 からの相対名。'\\' は xdotool で落ちるので使わない)。"""
    w = focus()
    if not w:
        log("set_drive: no M88 window")
        return False
    pe("xdotool key --delay 200 alt+d %sReturn" % ("Down " * (n - 1)))
    # ダイアログが開いたのを確かめてから打つ(開く前に打つと M88 のメニューへキーが入る)。
    # Xvfb にはウィンドウマネージャが無く、フォーカスはダイアログへ移らないので明示的に移す
    t0 = time.time()
    while not dlg():
        if time.time() - t0 > 10:
            log("set_drive %d: dialog did not open" % n)
            shot(os.path.join(SHOTS, "drive%d_fail.png" % n))
            return False
        time.sleep(0.5)
    d = dlg()
    pe("xdotool windowactivate --sync %s 2>/dev/null || xdotool windowfocus --sync %s; sleep 0.3; xdotool type --delay 80 '%s'; sleep 0.3; xdotool key Return" % (d, d, fname))
    t0 = time.time()
    while dlg():
        if time.time() - t0 > 10:
            log("set_drive %d: dialog did not close" % n)
            shot(os.path.join(SHOTS, "drive%d_fail.png" % n))
            pe("xdotool windowfocus %s; xdotool key Escape" % d)
            return False
        time.sleep(0.5)
    log("set_drive %d: %s" % (n, fname))
    return True


def key(name, hold=float(os.environ.get("WINERUN_HOLD", "0.12"))):
    pe("xdotool keydown %s; sleep %s; xdotool keyup %s" % (name, hold, name))


# xdotool のキー名 → M88 の画面に出る文字(Wine 上で実測)。2026-10-12 までは数字・記号が 1 つずれて見えたが、
# 原因は console.c の kn[] で '\\' が落ちていたことだった(修正済み。以前の補正表はそのバグを写していた)。
# 逆引きして「出したい文字 → 押すキー」にする。
_KEYMAP = {
    "1": '1', "2": '2', "3": '3', "4": '4', "5": '5', "6": '6', "7": '7', "8": '8', "9": '9', "0": '0',
    "minus": '-', "equal": ';', "bracketleft": '[', "bracketright": ']', "backslash": "\\", "semicolon": ':',
    "apostrophe": '^', "grave": '@', "comma": ',', "period": '.', "slash": '/', "space": " ",
    "S-minus": "=", "S-equal": "+", "S-bracketleft": "{", "S-bracketright": "}", "S-backslash": "|",
    "S-semicolon": "*", "S-apostrophe": "~", "S-grave": "`", "S-comma": "<", "S-period": ">", "S-slash": "?",
    "S-1": "!", "S-2": '"', "S-3": "#", "S-4": "$", "S-5": "%", "S-6": "&", "S-7": "'", "S-8": "(", "S-9": ")",
}
CHARKEY = {v: k for k, v in _KEYMAP.items()}
for _c in "abcdefghijklmnopqrstuvwxyz":
    CHARKEY[_c] = _c
    CHARKEY[_c.upper()] = "S-" + _c


def type_line(s, hold=float(os.environ.get("WINERUN_HOLD", "0.12")), ret=True):
    seq = []
    for ch in s:
        k = CHARKEY.get(ch)
        if k is None:
            log("type_line: no key for %r" % ch)
            continue
        if k.startswith("S-"):
            k = k[2:]
            seq.append("xdotool keydown Shift_L; sleep 0.05; xdotool keydown %s; sleep %s; xdotool keyup %s; xdotool keyup Shift_L; sleep 0.05" % (k, hold, k))
        else:
            seq.append("xdotool keydown %s; sleep %s; xdotool keyup %s; sleep 0.05" % (k, hold, k))
    if ret:
        seq.append("xdotool keydown Return; sleep %s; xdotool keyup Return" % hold)
    pe("; ".join(seq))


def shot(path=None):
    os.makedirs(SHOTS, exist_ok=True)
    path = path or os.path.join(SHOTS, "shot.png")
    pe("import -window root /pc88/_shot.png")
    subprocess.run(["cp", "-f", os.path.join(WORK, "_shot.png"), path])
    print(path)


_screen = {}


class _Mem:
    """/proc/<pid>/mem の読み取り。Yama(ptrace_scope=1)で子孫以外は拒否されるので、その時だけ sudo dd にする。"""
    def __init__(self, pid):
        self.pid, self.pos, self.f = pid, 0, None
        try:
            self.f = open("/proc/%d/mem" % pid, "rb", 0)
        except PermissionError:
            pass

    def seek(self, a):
        self.pos = a
        if self.f:
            self.f.seek(a)

    def read(self, n):
        if self.f:
            return self.f.read(n)
        r = subprocess.run(["sudo", "-n", "dd", "if=/proc/%d/mem" % self.pid, "iflag=skip_bytes,count_bytes",
                            "skip=%d" % self.pos, "count=%d" % n, "bs=65536", "status=none"], capture_output=True)
        if r.returncode != 0 and not r.stdout:
            raise OSError("dd failed")
        return r.stdout


def find_screen():
    """TVRAM 行 3 の先頭アドレス(TIZIX IPL の下 2 行目に FAT Drive があるもの)を探す。"""
    pid = m88_pid()
    if pid is None:
        return None
    pat = b"TIZIX IPL"
    fallback = None
    try:
        mem = _Mem(pid)
        for line in open("/proc/%d/maps" % pid):
            m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)", line)
            if not m or "r" not in m.group(3):
                continue
            a, b = int(m.group(1), 16), int(m.group(2), 16)
            if b - a > 0x20000000:
                continue
            try:
                mem.seek(a)
                buf = mem.read(b - a)
            except (OSError, ValueError, OverflowError):
                continue
            i = 0
            while True:
                i = buf.find(pat, i)
                if i < 0:
                    break
                if buf[i + 240:i + 249] == b"FAT Drive":
                    _screen["pid"], _screen["addr"] = pid, a + i
                    return a + i
                # 起動後は FAT Drive の行が消えるので、TVRAM のレイアウト(80 文字 + 属性 40 バイト)で代用する
                if buf[i + 80:i + 84] == b"\x80\x00\x80\x00" and fallback is None:
                    fallback = a + i
                i += 1
    except OSError:
        return None
    if fallback is not None:
        _screen["pid"], _screen["addr"] = pid, fallback
    return fallback


def screen():
    addr = _screen.get("addr") or find_screen()
    if addr is None:
        return None
    mem = _Mem(_screen["pid"])
    mem.seek(addr - 360)
    buf = mem.read(25 * PITCH)
    rows = []
    for r in range(25):
        line = "".join(chr(c) if 0x20 <= c < 0x7F else " " for c in buf[r * PITCH:r * PITCH + 80]).rstrip()
        rows.append(line)
    return [l for l in rows if l]


def run_cmd(cmd, timeout=40):
    type_line(cmd)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.7)
        scr = screen() or []
        ci = -1
        for i, l in enumerate(scr):
            if re.search(re.escape("# " + cmd) + r"\s*$", l):
                ci = i
        if ci >= 0 and any(re.match(r"^\[.*\]#\s*$", l) for l in scr[ci + 1:]):
            out = []
            for l in scr[ci + 1:]:
                if re.match(r"^\[.*\]#", l):
                    break
                out.append(l)
            return out
    return None


def warmup():
    """boot 直後は起動処理で打鍵を取りこぼす(先頭の 10 文字ほどが落ちる)。空の Return を捨て打ちし、echo が返るまで待つ。"""
    for _ in range(3):
        key("Return")
        time.sleep(1.5)
    for _ in range(6):
        out = run_cmd("echo vtready", 25)
        if out is not None and any("vtready" in l for l in out):
            break
    time.sleep(2)


def waitfor(rx, timeout=40):
    """画面(TVRAM の文字)に正規表現 rx の行が出るまで待つ。固定 sleep では vi の起動(オーバーレイ読み込み)に間に合わないため。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        scr = screen() or []
        if any(re.search(rx, l) for l in scr):
            return True
        time.sleep(0.7)
    return False


def vitest():
    """vi で hello を書いて :wq し、cat で hello が読めるかを確認する(画面の状態を待ちながら進める)。"""
    name = "vt%d" % (int(time.time()) % 100000)
    path = "/root/" + name
    # boot 直後は起動処理で打鍵を取りこぼす(先頭の 10 文字ほどが落ちる)。echo が返るまでウォームアップする。
    warmup()
    type_line("vi " + path)
    if not waitfor("^" + re.escape(path), 90):
        print("FAIL vi: vi が起動しない(最下行にファイル名が出ない)")
        return False
    time.sleep(1.5)
    type_line("i", ret=False)
    if not waitfor("INSERT", 30):
        print("FAIL vi: -- INSERT -- にならない")
        return False
    time.sleep(3)
    for c in "hello":
        type_line(c, ret=False)
        time.sleep(3)
    key("Escape")
    time.sleep(3)
    for c in ":wq":
        type_line(c, ret=False)
        time.sleep(1)
    key("Return")
    if not waitfor("written", 30):
        print("FAIL vi: :wq の後に written が出ない")
        return False
    time.sleep(1.5)
    out = run_cmd("cat " + path)
    ok = out is not None and any("hello" in l for l in out)
    print("%s vi hello => %s" % ("PASS" if ok else "FAIL", " / ".join(out) if out else "(timeout)"))
    return ok


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    if a[0] == "boot":
        ok = boot()
        print("booted" if ok else "boot timeout")
        sys.exit(0 if ok else 1)
    if a[0] == "run":
        for c in a[1:]:
            print("#", c)
            print("\n".join(run_cmd(c) or ["(no output / timeout)"]))
    elif a[0] == "text":                   # Return なしで文字列を打つ(vi の入力モード用)
        type_line(a[1], ret=False)
    elif a[0] == "key":                    # 名前でキーを押す(Escape / Return など)
        for n in a[1:]:
            key(n)
            time.sleep(0.3)
    elif a[0] == "waitfor":
        sys.exit(0 if waitfor(a[1], float(a[2]) if len(a) > 2 else 40) else 1)
    elif a[0] == "vitest":
        sys.exit(0 if vitest() else 1)
    elif a[0] == "shot":
        shot(a[1] if len(a) > 1 else None)
    elif a[0] == "tests":
        cases = [l.rstrip("\n").split("\t", 1) for l in open(a[1], encoding="utf-8") if l.strip() and not l.startswith("#")]
        fail = 0
        warmup()                              # boot 直後の取りこぼしを、テスト対象の外で吸収する
        for cmd, *e in cases:
            if cmd.strip() == "@vitest":      # 特殊行: vi で hello を保存して cat で確認(画面の状態を待ちながら進める)
                ok = vitest()
                fail += 0 if ok else 1
                continue
            exp = e[0].strip() if e else ""
            out = run_cmd(cmd)
            body = "\n".join(out or [])
            # 期待文字列が空なら「出力が無いこと」を判定する(エラー表示を見逃さない)。None はタイムアウト
            if exp.startswith("!"):            # "!文字列" = 含まれていないこと(rm の結果など)
                ok = out is not None and exp[1:] not in body
            else:
                ok = out is not None and (exp in body if exp else body.strip() == "")
            fail += 0 if ok else 1
            print("%s %s => %s" % ("PASS" if ok else "FAIL", cmd, " / ".join(out) if out else ("(no output)" if out is not None else "(timeout)")))
        print("RESULT: %d / %d PASS" % (len(cases) - fail, len(cases)))
        sys.exit(1 if fail else 0)


main()
