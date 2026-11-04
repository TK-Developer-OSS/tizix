# arch/m68k-x68000: tizix on the Sharp X68000

tizix boots from a 2HD floppy image on the X68000 (XM6G with a real IPL-ROM, or hardware) and runs
the common kernel, the shell and the external commands (`/bin/*.bin`). The console and the disk go
through the ROM's IOCS (`TRAP #15`), so no X68000 hardware is programmed directly.

Verified with: XM6G (Type G) + IPLROM.DAT + CGROM.DAT, under Wine. **Not yet tried on real hardware.**

## Build

```
make -C arch/m68k-x68000        # kernel.bin, boot_ipl.bin, disk.xdf in build/arch/m68k-x68000/obj/
```

Needs `m68k-elf-gcc` 8.x (default `~/tools/m68k/bin`, override with `M68K=`), `mtools`, `dosfstools`
and Python 3. The image is `build/arch/m68k-x68000/obj/disk.xdf` (77 x 2 x 8 sectors of 1024 bytes,
FAT12, 1,261,568 bytes).

## Run

1. Put `IPLROM.DAT` and `CGROM.DAT` where XM6G finds them.
2. Open `disk.xdf` in FDD0 and reset. XM6G shows a compatibility warning when the CGROM is not a
   genuine one; choose "continue".
3. `TIZIX BOOT` appears, then the kernel banner and the `[/root]# ` prompt.

Regression (no host keyboard / mouse / screen; XM6G runs on its own Xvfb display):

```
arch/m68k-x68000/tools/xm6run.py tests arch/m68k-x68000/tools/x68k_tests.txt
```

The console text is read from a ring the kernel keeps at `$0FF000` (`console.c`), through
`/proc/<pid>/mem` of the XM6G process, so the check does not depend on the screen. XM6 stores main
RAM with every 2 bytes swapped; `xm6run.py` finds the ring by its reversed magic.

## How it boots

| Step | Where | What |
|------|-------|------|
| 1 | ROM IPL | reads sector 0 of FDD0 to `$2000` and jumps to `$2000` |
| 2 | `boot_ipl.s` (sector 0, offset `$3E`) | walks the FAT12 root for `TIZIX.SYS`, loads it at `$10000` with IOCS `_B_READ`, jumps |
| 3 | `crt0.s` | clears bss, installs vectors 2-11 (exception report), TRAP #0/#1 and the Timer-C hook; the ROM's own vectors (IOCS etc.) stay |
| 4 | `kmain.c` | common kernel `init()` -> mounts FAT -> starts `/bin/sh.bin` |

Sector 0 has the layout of a genuine X68000 disk: `60 3C 90` + `"X68IPL30"` at offset 3 + a standard
FAT BPB (1024 B/sector, 1 reserved, 2 FATs x 2 sectors, 192 root entries, media `FE`). The Sharp
boot code is not used (the loader is written from scratch); `$55AA` is added at offset 510 because
FatFs wants it. FatFs also accepts the `$60` (68000 `bra.s`) first byte (`FF_JMPBOOT_BRA68K`).

## Memory map (1 MB minimum)

| Range | Use |
|-------|-----|
| `$000000-$00FFFF` | ROM vector table, IOCS work area (not touched, except the vectors listed above) |
| `$010000-$018FFF` | kernel (slot 0): image + bss, 36 KB |
| `$019000-$0F8FFF` | slots 1..28: external commands, 32 KB each |
| `$0F9000-$0FEFFF` | kernel stack (top `$0FF000`) |
| `$0FF000-$0FFFFF` | console output ring (test aid) |

## IOCS facts used (verified on XM6G)

- `TRAP #15` with `d0` = call number; `d0` is the only result.
- `$20` `_B_PUTC` (`d1` = char; ESC sequences work), `$21` `_B_PRINT` (`a1` = string),
  `$00` `_B_KEYINP`, `$01` `_B_KEYSNS`, `$8E` boot device (`(d0 << 8) | $70` is the `d1` device code).
- `$46` `_B_READ`, `$45` `_B_WRITE`: `d1.w` = device, `d2.l` = `N C H R` bytes (`$03CCHHRR`, R is
  1-origin, N=3 is 1024 bytes), `d3` = bytes, `a1` = buffer. Only **bit 30** of the result is an
  error; the other bits are status (e.g. the top byte `$20` is the FDC ST3 "ready").
- Disk I/O waits for FDC / DMA interrupts, so `diskio.c` lowers the interrupt mask for the call
  and stops task switching meanwhile (`plat_noswitch`); the Timer-C tick still counts.

## Timer

`TRAP`-less: the ROM already runs MFP Timer-C at 100 Hz (vector `$45`, address `$114`). `crt0.s`
hooks it, calls the ROM handler first (as an interrupt) and then does the kernel tick and the
round-robin switch. `TICK_HZ` is 100.

## Keyboard

Arrow keys become `ESC [ A..D`. Other keys without a character are dropped.

## Not done / known

- Real hardware is untested (IOCS only, so it should work; FDD timing is unverified).
- Only FDD0 as a FAT12 volume; no hard disk (SASI/SCSI), no RTC (the clock starts at 1970-01-01).
- Memory above 1 MB is not used.
