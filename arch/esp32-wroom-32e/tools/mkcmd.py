#!/usr/bin/env python3
"""mkcmd.py - 外部コマンドの ELF 3 本から、再配置表つきの .bin を作る(task.md #108)

    mkcmd.py A.elf B.elf C.elf OUT.bin TSHIFT DSHIFT TEXT_MAX DATA_MAX

  A = 基準の番地(slot1 の枠)でリンクしたもの
  B = コードの番地だけ TSHIFT ずらしたもの
  C = データの番地だけ DSHIFT ずらしたもの

Xtensa のコードは絶対番地を命令に埋めず、32bit の語(リテラル、データ中のポインタ)に
置く。だから A と B で値が変わった語は「コード枠の番地が入っている語」、A と C で
変わった語は「データ枠の番地が入っている語」で、それが再配置の全部になる。
リンカの再配置情報は読まない ── 出来上がった像そのものを比べるので、見落としが
あれば(語の境界に乗っていない、差がずらした量と違う)ここで止まる。

.bin の形は arch/esp32-wroom-32e/loader.c の冒頭を参照(両者を一致させること)。
"""
import struct
import sys

MAGIC = 0x31585A54          # 'TZX1'
AV_BYTES = 32 * 4 + 0x2C0 + 32   # src/loader.h の KEXEC_ARGV_BYTES(argv[] + 引数文字列)
STACK_MIN = 0x800           # loader.c の STACK_MIN


def die(msg):
    sys.exit("mkcmd.py: " + msg)


def load(path):
    """(entry, {名前: (addr, size, 中身)}) を返す。NOBITS は中身が空。"""
    d = open(path, "rb").read()
    if d[:6] != b"\x7fELF\x01\x01":
        die(path + ": 32bit リトルエンディアンの ELF ではない")
    entry, = struct.unpack_from("<I", d, 24)
    shoff, = struct.unpack_from("<I", d, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
    sh = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
    stroff = sh[shstrndx][4]
    out = {}
    for name, typ, _flags, addr, off, size, *_rest in sh:
        end = d.index(b"\0", stroff + name)
        out[d[stroff + name:end].decode()] = (addr, size, b"" if typ == 8 else d[off:off + size])
    return entry, out


def sect(secs, name):
    return secs.get(name, (0, 0, b""))


def relocs(name, a, b, c, tshift, dshift, in_data):
    if not (len(a) == len(b) == len(c)):
        die("%s: 番地を変えたら大きさが変わった(%d / %d / %d)" % (name, len(a), len(b), len(c)))
    if len(a) % 4:
        die("%s: 大きさが 4 の倍数でない" % name)
    out = []
    for i in range(0, len(a), 4):
        wa, = struct.unpack_from("<I", a, i)
        wb, = struct.unpack_from("<I", b, i)
        wc, = struct.unpack_from("<I", c, i)
        if wb == wa and wc == wa:
            continue
        if wc == wa and (wb - wa) & 0xFFFFFFFF == tshift:
            kind = 0
        elif wb == wa and (wc - wa) & 0xFFFFFFFF == dshift:
            kind = 0x4000
        else:
            die("%s+0x%x: 再配置できない語(%08x / %08x / %08x)。語の境界に乗っていない"
                "ポインタか、番地を命令に埋めたコードがある" % (name, i, wa, wb, wc))
        if i // 4 > 0x3FFF:
            die("%s: 大きすぎる" % name)
        out.append((0x8000 if in_data else 0) | kind | (i // 4))
    return out


def main():
    if len(sys.argv) != 9:
        die("usage: mkcmd.py A.elf B.elf C.elf OUT.bin TSHIFT DSHIFT TEXT_MAX DATA_MAX")
    tshift, dshift, text_max, data_max = (int(x, 0) for x in sys.argv[5:9])
    ea, sa = load(sys.argv[1])
    eb, sb = load(sys.argv[2])
    ec, sc = load(sys.argv[3])

    taddr, _, text = sect(sa, ".text")
    _, _, data = sect(sa, ".data")
    _, bss, _ = sect(sa, ".bss")
    if (eb - ea) & 0xFFFFFFFF != tshift or ec != ea:
        die("entry がコードと一緒に動いていない")

    rel = relocs(".text", text, sect(sb, ".text")[2], sect(sc, ".text")[2], tshift, dshift, False)
    rel += relocs(".data", data, sect(sb, ".data")[2], sect(sc, ".data")[2], tshift, dshift, True)

    if len(text) > text_max:
        die("コードが枠を超えた: %d > %d(Makefile の UTSIZE)" % (len(text), text_max))
    need = ((len(data) + bss + 3) & ~3) + AV_BYTES + STACK_MIN
    if need > data_max:
        die("データ+BSS が枠を超えた: %d + argv/スタック %d > %d(Makefile の UDSIZE)"
            % (len(data) + bss, AV_BYTES + STACK_MIN, data_max))

    with open(sys.argv[4], "wb") as f:
        f.write(struct.pack("<6I", MAGIC, ea - taddr, len(text), len(data), bss, len(rel)))
        f.write(text)
        f.write(data)
        f.write(struct.pack("<%dH" % len(rel), *rel))


if __name__ == "__main__":
    main()
