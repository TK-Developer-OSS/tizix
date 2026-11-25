# arch/z80-zxspectrum/plus3 - ZX Spectrum +3

tizix on a ZX Spectrum +3 (Z80 3.5MHz, 128KB RAM used as all-RAM configuration 0, uPD765 floppy, bitmap screen), tested on ZEsarUX 13.0.

- `make ARCH=z80-zxspectrum DEVICE=plus3` builds `disks/disk3.dsk` (a "MV - CPC" DSK, 40 tracks x 9 x 512B). `python3 python/test_z80_zxspectrum.py` boots it on ZEsarUX (no window), types over ZRCP and checks the screen text.
- `make ARCH=z80-zxspectrum run` opens ZEsarUX in a window (needs `DISPLAY`; `ZESARUX` = the binary, `ZX_LIBS` = a directory holding libSDL-1.2.so.0 if the system has none).
- Files: `boot.s` (the boot sector, 0xFE10), `hi.s` (screen code, uPD765 driver, font and the text shadow, loaded to 0xF000), `crt0.s`, `console.c` (cursor, ESC parsing, keyboard scan), `diskio.c` (FatFs side of the disk), `mkdisk.py` (assembles the DSK), `tools/genfont.py` (makes `font.inc` from the TI-84 Plus 5x7 font).
- Memory: kernel 0x0100-0x7FFF, process blocks 0x8000-0xEFFF, 0xF000-0xFFFF = hi.s (code F000, text shadow F400, font F800, boot sector F E00 until the kernel is running).
- No key repeat, 32 columns, no real-hardware test.
