# arch/z80-amstradcpc/cpc6128 - Amstrad CPC 6128

tizix on an Amstrad CPC 6128 (Z80 4MHz, 128KB RAM used as RAM configuration 1 with both ROMs off, uPD765 floppy, mode 2 screen), tested on MAME 0.264 (`cpc6128`); it also boots on Caprice32.

- `make ARCH=z80-amstradcpc DEVICE=cpc6128` builds `disks/cpc.dsk` (a "MV - CPC" DSK in "system format": 40 tracks x 9 x 512B, sector ids 0x41-0x49). Boot it with `|CPM` at the BASIC prompt.
- `python3 python/test_z80_amstradcpc.py` boots it on MAME (no window), types through MAME's natural keyboard (`python/test_z80_amstradcpc.lua`) and checks the screen text. MAME needs `cpc6128.rom` and `cpcados.rom` on its rompath (Caprice32's `rom/cpc6128.rom` and `rom/amsdos.rom` are the same files).
- `make ARCH=z80-amstradcpc run` opens MAME in a window and types `|CPM` (needs `DISPLAY`; `MAME` = the binary). The host keys stand for the CPC keys at the same place, so the symbols are the ones on a CPC keyboard (Shift + 6 = `&`; a Japanese keyboard has nearly the same layout). Caprice32 also boots the disk, but leaves a key held down when Shift is released before a shifted key.
- Files: `boot.s` (the boot sector, moves itself to 0x9000), `hi.s` (screen code, uPD765 driver, font, key tables and the text shadow, loaded to 0xF000), `crt0.s` (the 300Hz interrupt, five in six only return), `console.c` (cursor, ESC parsing, keyboard scan), `diskio.c` (FatFs side of the disk), `mkdisk.py` (assembles the DSK). `font.inc` is the font of arch/z80-zxspectrum.
- Memory: kernel 0x0100-0x7FFF, process blocks 0x8000-0xEFFF, 0xF000-0xFFFF = hi.s in bank 7 (code F000, text shadow F400, font FC00, key tables FF00). The screen is bank 3, paged in at 0x4000 (configuration 3) only inside hi.s. Banks 4-6 are not used yet.
- 80 x 25 text, no key repeat, no real-hardware test.
