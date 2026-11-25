#!/usr/bin/env python3
"""dsk.py - write a standard "MV - CPC" DSK image (the +3 format): 40 tracks, 1 side, 9 x 512B sectors, ids 1..9."""
import struct

TRACKS, SPT, SSZ = 40, 9, 512

def write_dsk(path, img):
    """img = the raw disk, track after track (TRACKS*SPT*SSZ bytes)."""
    assert len(img) == TRACKS * SPT * SSZ
    out = bytearray(b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n")
    out += b"tizix".ljust(14, b"\0")
    out += bytes([TRACKS, 1]) + struct.pack("<H", 0x100 + SPT * SSZ)
    out += bytes(256 - len(out))
    for t in range(TRACKS):
        h = bytearray(b"Track-Info\r\n\0")
        h += bytes(3) + bytes([t, 0, 0, 0, 2, SPT, 0x4E, 0xE5])
        for s in range(SPT):
            h += bytes([t, 0, s + 1, 2, 0, 0]) + struct.pack("<H", SSZ)
        h += bytes(256 - len(h))
        out += h + img[t * SPT * SSZ:(t + 1) * SPT * SSZ]
    open(path, "wb").write(out)
