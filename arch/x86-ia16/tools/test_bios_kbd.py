#!/usr/bin/env python3
import os
import socket
import subprocess
import time
import sys

SOCK_PATH = "/tmp/qemu-bios-mon.sock"
IMG_PATH = "arch/x86-ia16/disk/floppy.img"

def send_monitor_cmd(sock, cmd):
    sock.sendall((cmd + "\n").encode("utf-8"))
    time.sleep(0.2)

def main():
    if not os.path.exists(IMG_PATH):
        print(f"Error: {IMG_PATH} not found. Run 'make ARCH=x86-ia16 all' first.")
        sys.exit(1)

    if os.path.exists(SOCK_PATH):
        os.remove(SOCK_PATH)

    cmd = [
        "qemu-system-i386",
        "-drive", f"file={IMG_PATH},format=raw,if=floppy",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-chardev", "stdio,id=c0,signal=off",
        "-serial", "chardev:c0",
        "-monitor", f"unix:{SOCK_PATH},server,nowait",
        "-display", "none",
        "-no-reboot",
        "-boot", "a"
    ]

    print("Starting QEMU for BIOS kbd test...")
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    # Wait for monitor socket to appear
    sock = None
    for _ in range(30):
        if os.path.exists(SOCK_PATH):
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.connect(SOCK_PATH)
                break
            except socket.error:
                if sock:
                    sock.close()
                sock = None
        time.sleep(0.2)

    if not sock:
        print("Error: QEMU monitor socket did not appear.")
        proc.terminate()
        sys.exit(1)

    # Wait for tizix boot / shell prompt
    print("Waiting for boot...")
    time.sleep(8.0)

    # Send keys: "echo a\n"
    keys = ["e", "c", "h", "o", "spc", "a", "ret"]
    print(f"Sending keys via QEMU monitor: {keys}")
    for k in keys:
        send_monitor_cmd(sock, f"sendkey {k}")
        time.sleep(0.1)

    time.sleep(1.5)

    # Quit QEMU
    try:
        send_monitor_cmd(sock, "quit")
    except Exception:
        pass

    sock.close()
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    stdout_data, _ = proc.communicate()
    print("--- QEMU STDOUT ---")
    print(stdout_data)
    print("-------------------")

    import re
    if re.search(r"echo a\r?\n(?:.*\r?\n)?a\r?\n\[/root\]# ", stdout_data):
        print("SUCCESS: BIOS kbd input 'echo a' executed and output 'a' found.")
        sys.exit(0)
    print("FAIL: expected 'echo a' echo followed by output 'a' not found.")
    sys.exit(1)

if __name__ == "__main__":
    main()

