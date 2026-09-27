"""tizix ハーネス共通パス。

arch 分離(src/ と arch/$ARCH/)に伴い、cpmsim バイナリと FAT/boot ディスクは
arch/$ARCH/ 配下へ移動した。各テストスクリプトがパスを直書きしないよう、ここに
一元化する。環境変数 TIZIX_ARCH で切替(既定 z80pack)。

使い方:
    import os, sys
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "python"))
    #   ↑ スクリプトが python/ の中なら "python" は付けない
    import tzpaths
    proc = subprocess.Popen(tzpaths.CPMSIM_CMD, cwd=tzpaths.CPMSIM_CWD, ...)
    tzpaths.wait_disk_ready(tzpaths.DRIVEB)
"""
import os
import subprocess
import time

TIZIX    = os.environ.get("TIZIX_ROOT", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARCH     = os.environ.get("TIZIX_ARCH", "z80pack")
ARCH_DIR = os.path.join(TIZIX, "arch", ARCH)

DISKS_DIR = os.path.join(ARCH_DIR, "disks")
DRIVEA    = os.path.join(DISKS_DIR, "drivea.dsk")
DRIVEB    = os.path.join(DISKS_DIR, "driveb.dsk")

# cpmsim は ARCH_DIR に置いた実体を、その場を cwd にして "-d disks" で起動する。
CPMSIM_CWD = ARCH_DIR
CPMSIM_CMD = ["./cpmsim", "-d", "disks"]
DRIVEB_SIZE = 256256

# z80board(#71): z80boardsim は ROM 像ではなく kernel.ihx を -x で受け取り、
#   SD カードは arch/z80board/sdcard.img(FAT)。TIZIX_ARCH=z80board で
#   既存の pty テスト(test_vi 等)を実機相当の SD/SPI ドライバ経路で回せる。
if ARCH == "z80board":
    DRIVEB = os.path.join(ARCH_DIR, "sdcard.img")
    CPMSIM_CMD = ["./z80boardsim", "-x", "../../build/arch/z80board/obj/kernel.ihx"]
    DRIVEB_SIZE = None

# m68k-mega: m68ksim(カーネル像 + FAT ディスク)。TIZIX_ARCH=m68k-mega で
#   z80 用の pty テストがそのまま m68ksim で回る(落ちたものがアーキ間の差)。
if ARCH == "m68k-mega":
    DRIVEB = os.path.join(TIZIX, "build", "arch", ARCH, "obj", "disk.img")
    CPMSIM_CMD = ["./m68ksim", "../../build/arch/m68k-mega/obj/kernel.bin", "../../build/arch/m68k-mega/obj/disk.img"]
    DRIVEB_SIZE = None


def wait_disk_ready(path=DRIVEB, want_size=DRIVEB_SIZE, timeout=30.0):
    """FAT ディスクイメージの書き込みが落ち着くまでブロックする。

    `make` (mkfatdisk.sh) はページキャッシュ経由で書くため、\\\\rocky9 共有では
    write-back が遅れ、直後に起動した cpmsim が途中書きイメージを読む
    (FAT mount 失敗 / 偽 reboot)。sync 後、サイズと mtime が数サンプル
    変化しなくなるまでポーリングする。カーネルのバグではなくホスト側の競合。
    """
    subprocess.run(["sync"], check=False)
    t0 = time.time()
    last = None
    stable = 0
    while time.time() - t0 < timeout:
        try:
            st = os.stat(path)
        except FileNotFoundError:
            time.sleep(0.2)
            continue
        sig = (st.st_size, st.st_mtime_ns)
        if (want_size is None or st.st_size == want_size) and sig == last:
            stable += 1
            if stable >= 3:
                return True
        else:
            stable = 0
        last = sig
        time.sleep(0.2)
    return False
