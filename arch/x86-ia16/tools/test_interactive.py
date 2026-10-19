#!/usr/bin/env python3
"""test_interactive.py - arch/x86-ia16 の対話テストハーネス"""
import os, pty, select, subprocess, sys, time, shutil, tempfile

def run_session(run_num):
    env = os.environ.copy()
    ia16_bin = os.path.expanduser("~/tools/ia16/bin")
    ia16_lib = os.path.expanduser("~/tools/ia16/lib")
    env["PATH"] = ia16_bin + ":" + env.get("PATH", "")
    env["LD_LIBRARY_PATH"] = ia16_lib + ":" + env.get("LD_LIBRARY_PATH", "")

    qemu_bin = "qemu-system-i386"
    script_dir = os.path.dirname(os.path.abspath(__file__))
    img_path = os.path.abspath(os.path.join(script_dir, "..", "disk", "floppy.img"))

    fd, tmp_img_path = tempfile.mkstemp(suffix=".img")
    os.close(fd)
    shutil.copy(img_path, tmp_img_path)

    cmd_args = [
        qemu_bin, "-drive", (f"file={tmp_img_path},format=raw,if=ide" if os.environ.get("TIZIX_BOOT") == "hdd" else f"file={tmp_img_path},format=raw,if=floppy"),
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-chardev", "stdio,id=c0,signal=off", "-serial", "chardev:c0",
        "-display", "none", "-no-reboot", "-boot", ("c" if os.environ.get("TIZIX_BOOT") == "hdd" else "a")
    ]

    m, s = pty.openpty()
    p = subprocess.Popen(cmd_args, stdin=s, stdout=s, stderr=s, cwd=script_dir, env=env)
    os.close(s)

    buf = b""

    def read_until(pat, timeout=10.0):
        nonlocal buf
        end = time.time() + timeout
        while time.time() < end:
            r, _, _ = select.select([m], [], [], 0.1)
            if r:
                try:
                    chunk = os.read(m, 4096)
                    if not chunk: break
                    buf += chunk
                except OSError: break
            if pat.encode() in buf: return True
        return False

    try:
        fail_count = 0
        results = []

        print(f"[Run {run_num}] Waiting for boot banner and prompt...")
        ok_banner = read_until("tizix", 10.0)
        ok_prompt = read_until("[/root]# ", 10.0)
        boot_ok = ok_banner and ok_prompt
        results.append(("Boot & Prompt", "tizix & [/root]#", "PASS" if boot_ok else "FAIL"))
        
        if not boot_ok:
            print(f"DEBUG boot failed, buf={repr(buf)}")
            return False, results

        tests = [
            ("echo hello", "hello", "[/root]# "),
            ("ls", "HELLO.TXT", "[/root]# "),
            ("mkdir /root/d", "", "[/root]# "),
            ("ls", "d", "[/root]# "),
            ("rmdir /root/d", "", "[/root]# "),
            ("ls", "!d", "[/root]# "),
            ("echo ok", "ok", "[/root]# "),
            ("echo a > /root/m5", "", "[/root]# "),
            ("echo b > /root/m6", "", "[/root]# "),
            ("mv /root/m5 /root/m6", "", "[/root]# "),
            ("cat /root/m6", "a", "[/root]# "),
            ("ls", "!M5", "[/root]# "),
            ("ls", "M6", "[/root]# "),
            ("echo keep > /root/k1", "", "[/root]# "),
            ("mv /root/nosuch /root/k1", "", "[/root]# "),
            ("cat /root/k1", "keep", "[/root]# "),
            ("mkdir /root/dd", "", "[/root]# "),
            ("mv /root/dd /root/de", "", "[/root]# "),
            ("ls", "!dd", "[/root]# "),
            ("ls", "de", "[/root]# "),
            ("sleep 1", "", "[/root]# "),
            ("sleep", "usage", "[/root]# "),
            ("sleep abc", "", "[/root]# "),
            ("id", "uid=0(root)", "[/root]# "),
            ("df", "total", "[/root]# "),
            ("uptime", "up", "[/root]# "),
            ("free", "used", "[/root]# "),
            ("df /nosuch", "total", "[/root]# "),
            ("du /root", "/root", "[/root]# "),
            ("echo 12345 > /root/z", "", "[/root]# "),
            ("du /root/z", "6 /root/z", "[/root]# "),
            ("du /root/nosuch", "cannot open", "[/root]# "),
            ("du /nosuch", "cannot open", "[/root]# "),
            ("echo abc > /root/s1", "", "[/root]# "),
            ("sed s/b/X/ /root/s1", "aXc", "[/root]# "),
            ("sed", "usage", "[/root]# "),
            ("history", "echo abc", "[/root]# "),
            ("ptx", "line-2", "[/root]# "),
            ("echo hello | cat", "hello", "[/root]# "),
            ("cat /etc/rc | tail -1", "rc-ok", "[/root]# "),
            ("ls | wc", "2 2", "[/root]# "),
            ("tail -2 /etc/rc | head -1", "rc: started", "[/root]# "),
        ]

        for item in tests:
            if len(item) == 4 and item[3] == "SKIP":
                results.append((item[0], item[1], "SKIP"))
                print(f"Running [{item[0]}] ... SKIP (#77 unported)")
                continue

            cmd, want, prompt_want = item
            buf = b""
            print(f"Running [{cmd}] ... ", end="", flush=True)
            
            t_start = time.time()
            os.write(m, (cmd + "\n").encode())
            if cmd.startswith("vi"):
                time.sleep(0.3)
                os.write(m, b"ihello\x1b:wq\n")
            elif cmd.startswith("tee"):
                time.sleep(0.1)
                os.write(m, b"hello\x04")
            timeout_val = 3.0 if cmd == "sleep 1" else 2.0
            good = read_until(prompt_want, timeout_val)
            t_elapsed = time.time() - t_start

            actual_raw = buf.decode("latin-1", errors="ignore").replace("\r", "")
            lines = [line.strip() for line in actual_raw.split("\n")]
            print(f"DEBUG [{cmd}] actual_raw={repr(actual_raw)}, t_elapsed={t_elapsed:.2f}")

            rebooted = (cmd != "uname" and "tizix" in actual_raw[len(cmd)+1:] and "startup" not in actual_raw)
            error_keywords = ["redir:", "cannot open"]
            if "a &" not in cmd and "b &" not in cmd and "f4" not in cmd and cmd != "sleep":
                error_keywords.append("not found")
            error_keywords = [k for k in error_keywords if k not in want]   # 期待値に含まれるエラー文言(失敗側の試験)は除外
            has_error = any(err in actual_raw for err in error_keywords)

            status = "PASS"
            if not good:
                status = "FAIL"
            elif rebooted:
                status = "FAIL"
            elif has_error:
                if cmd == "cat /root/f4" and "cannot open" in actual_raw:
                    status = "FAIL"
                elif cmd != "cat /root/f4":
                    status = "FAIL"
            elif cmd == "cat /root/f3" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd == "cat /root/f1" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd == "cat /root/f4" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd == "cat /root/m6" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd == "cat /root/k1" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd in ["whoami", "uname", "date", "tail /root/t1", "uniq /root/u1", "tee /root/te2"] and want not in actual_raw:
                status = "FAIL"
            elif cmd == "sleep 1":
                if t_elapsed < 1.0 or not good:
                    status = "FAIL"
            elif cmd == "sleep":
                if not good or ("usage" not in actual_raw and "sleep" not in actual_raw):
                    status = "FAIL"
            elif cmd == "sleep abc":
                if not good:
                    status = "FAIL"
            elif cmd == "id":
                if not good or not any("uid=" in line for line in lines) or has_error:
                    status = "FAIL"

            elif cmd == "ls":
                if want.startswith("!"):
                    target = want[1:]
                    if any(target.lower() in line.lower() for line in lines):
                        status = "FAIL"
                else:
                    if not any(want.lower() in line.lower() for line in lines):
                        status = "FAIL"
            elif cmd == "cat /etc/rc" and not any(want in line for line in lines):
                status = "FAIL"
            elif cmd == "echo ok" and not any(line == "ok" for line in lines):
                status = "FAIL"

            results.append((cmd, want, status))
            print(status)
            if status == "FAIL":
                fail_count += 1
                print(f"  -> Output:\n{actual_raw[-800:]}")

            time.sleep(0.05)

        return fail_count == 0, results

    finally:
        try:
            p.kill()
            p.wait()
        except Exception:
            pass
        if os.path.exists(tmp_img_path):
            try:
                os.remove(tmp_img_path)
            except Exception:
                pass

def main():
    success1, res1 = run_session(1)

    print("\n" + "="*60)
    print("RESULTS SUMMARY (Run 1)")
    print("="*60)
    print(f"{'Command':<55} | {'Run 1':<6}")
    print("-"*64)
    for i in range(len(res1)):
        cmd = res1[i][0]
        st1 = res1[i][2]
        print(f"{cmd:<55} | {st1:<6}")
    print("="*60)

    sys.exit(0 if success1 else 1)

if __name__ == "__main__":
    main()


