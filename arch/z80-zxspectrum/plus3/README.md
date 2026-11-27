# arch/z80-zxspectrum/plus3 - ZX Spectrum +3

tizix on a ZX Spectrum +3 (Z80 3.5MHz, 128KB RAM used as all-RAM configuration 0, uPD765 floppy, bitmap screen), tested on MAME 0.264 (`specpls3`).

- `make ARCH=z80-zxspectrum DEVICE=plus3` builds `disks/disk3.dsk` (a "MV - CPC" DSK, 40 tracks x 9 x 512B). `python3 python/test_z80_zxspectrum.py` boots it on MAME (no window), types with `tools/mamezx.lua` and checks the screen text (14 checks).
- `make ARCH=z80-zxspectrum run` opens MAME in a window (needs `DISPLAY`; `MAME` = the binary; the ROMs `40092.ic7` and `40093.ic8` on MAME's rompath, e.g. `~/mame/roms/specpls3/`: ZEsarUX's `p2a40.rom` is the two of them, 32KB each). `tools/mamezx.lua` boots the disk (MAME's +3 Loader does not) and turns each host key press into the character it gives on the host keyboard (`TZ_KBD=jp`, the default, or `us`), typed as the Spectrum keys that give it: `&`, `|`, `:` and the rest as printed on the keyboard, Esc, the arrow keys, Backspace, Ctrl + letter (Ctrl+C = Break). Alt + a key = Symbol Shift + that key (`|` = Alt+S, `\` = Alt+D, `_` = Alt+0 for the yen and ro keys MAME cannot see).
- Files: `boot.s` (the boot sector, 0xFE10), `hi.s` (screen code, uPD765 driver, font and the text shadow, loaded to 0xF000), `crt0.s`, `console.c` (cursor, ESC parsing, keyboard scan), `diskio.c` (FatFs side of the disk: the sectors wanted on one track in one command), `mkdisk.py` (assembles the DSK), `tools/genfont.py` (makes `font.inc` from the TI-84 Plus 5x7 font).
- Memory: kernel 0x0100-0x7FFF, process blocks 0x8000-0xEFFF, 0xF000-0xFFFF = hi.s (code F000, text shadow F400, font F800, boot sector F E00 until the kernel is running).
- No key repeat, 32 columns (vi is built with `TCOLS=31 VI_NO_NAME`), no real-hardware test.
