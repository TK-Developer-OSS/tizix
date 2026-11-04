"""make_xdf.py - build a bootable X68000 2HD floppy image (.xdf) for tizix.

Usage: make_xdf.py out.xdf boot_ipl.bin kernel.bin [user.bin ...]

Geometry: 77 cylinders x 2 heads x 8 sectors x 1024 bytes = 1,261,568 bytes.
The filesystem is FAT12 with the same BPB as a genuine Human68k 2HD disk
(1 reserved sector, 2 FATs x 2 sectors, 192 root entries, media 0xFE).
The ROM IPL loads sector 0 to $2000 and jumps to it; the header is
`60 3C 90 "X68IPL30"` followed by the BPB, and the boot code starts at 0x3E.
boot_ipl.bin loads TIZIX.SYS (the kernel) to $10000.
"""
import os
import subprocess
import sys

SECTOR = 1024
TOTAL = 1232
CODE_OFF = 0x3E


def run(*a, **k):
    return subprocess.run(list(a), check=True, **k)


def patch_boot_sector(dst, code):
    if CODE_OFF + len(code) > 510:
        raise RuntimeError("boot code too large: %d bytes" % len(code))
    with open(dst, "r+b") as f:
        bs = bytearray(f.read(SECTOR))
        hdr = bytearray(CODE_OFF)
        hdr[0:3] = bytes([0x60, 0x3C, 0x90])           # bra.s $3E ; nop
        hdr[3:11] = b"X68IPL30"
        hdr[0x0B:0x0D] = (SECTOR).to_bytes(2, "little")  # bytes/sector
        hdr[0x0D] = 1                                    # sectors/cluster
        hdr[0x0E:0x10] = (1).to_bytes(2, "little")       # reserved
        hdr[0x10] = 2                                    # FATs
        hdr[0x11:0x13] = (192).to_bytes(2, "little")     # root entries
        hdr[0x13:0x15] = TOTAL.to_bytes(2, "little")     # total sectors
        hdr[0x15] = 0xFE                                 # media
        hdr[0x16:0x18] = (2).to_bytes(2, "little")       # sectors/FAT
        hdr[0x18:0x1A] = (8).to_bytes(2, "little")       # sectors/track
        hdr[0x1A:0x1C] = (2).to_bytes(2, "little")       # heads
        hdr[0x2B:0x36] = b"TIZIX2HD   "
        hdr[0x36:0x3E] = b"FAT12   "
        bs[0:CODE_OFF] = hdr
        bs[CODE_OFF:] = bytes(SECTOR - CODE_OFF)
        bs[CODE_OFF:CODE_OFF + len(code)] = code
        bs[510:512] = b"\x55\xaa"   # FatFs wants it; the IPL does not care
        f.seek(0)
        f.write(bs)


def main():
    dst = sys.argv[1]
    boot_path, kernel_path = sys.argv[2], sys.argv[3]
    user_bins = sys.argv[4:]
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    if os.path.exists(dst):
        os.remove(dst)

    # mkfs.fat's own geometry guess is replaced by the real BPB values below
    run("mkfs.fat", "-F", "12", "-S", str(SECTOR), "-s", "1", "-R", "1",
        "-f", "2", "-r", "192", "-M", "0xFE", "-n", "TIZIX2HD",
        "-C", dst, str(TOTAL))

    # populate with mtools while the x86-style jump is still valid
    for d in ["::root", "::bin", "::etc", "::var", "::var/log", "::usr",
              "::usr/share", "::usr/share/tzsh"]:
        run("mmd", "-i", dst, d, capture_output=True)
    if os.path.exists("etc/rc"):
        run("mcopy", "-o", "-i", dst, "etc/rc", "::etc/rc")
    run("mcopy", "-o", "-i", dst, kernel_path, "::TIZIX.SYS")
    import glob
    for sh in sorted(glob.glob("../../user/tzsh/*.sh")):
        run("mcopy", "-o", "-i", dst, sh, "::usr/share/tzsh/")
    for p in user_bins:
        if os.path.exists(p):
            run("mcopy", "-o", "-i", dst, p, "::bin/" + os.path.basename(p))

    with open(boot_path, "rb") as f:
        patch_boot_sector(dst, f.read())
    print("Created %s (%d bytes)" % (dst, os.path.getsize(dst)))


if __name__ == "__main__":
    main()
