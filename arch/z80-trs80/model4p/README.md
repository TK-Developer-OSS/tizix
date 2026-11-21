# arch/z80-trs80/model4p - TRS-80 Model 4P

tizix on a TRS-80 Model 4P (Z80 4MHz, 64KB RAM, floppy, memory-mapped 80x24 text and keyboard), tested on xtrs 4.9d.

- `make ARCH=z80-trs80 DEVICE=model4p` builds `disks/disk4p-0` (JV3). `python3 python/test_z80_trs80.py` boots it on xtrs and runs the shell tests.
- Boot: the free 4P ROM reads track 0, id 1 (256B) to 0x4300; `boot.s` moves its second half to 0xF000 and reads the 127-sector kernel to 0x0100.
- Memory: map 2 (RAM 0000-F3FF, keyboard F400, video F800). Layout as `arch/msx`: kernel at 0x0100-0x7FFF, blocks from 0x8000, block 7 unused.
- Disk: 256B sectors, 18 a track; disk sector 1 = boot, sector 2 on = one 720KB FAT12 volume (FatFs sector s = 2+2s, 3+2s). WD1793 polled with interrupts off.
- Console: screen + keyboard (30Hz timer scan) and the UART (19200 8N1) as a second console; xtrs `-serial PTY` is what the test drives.
- Keys: Clear + `Q W E R T Y U I O P` gives `| \ [ ] { } ~ \` _ ^`; Break = Ctrl+C; Left = backspace; Shift+arrows = cursor keys.
- Not done: key repeat, cursor, real machine, writing back to a real floppy, vi tuned for the screen.
- Run it by hand: ``make ARCH=z80-trs80 DEVICE=model4p run` (or `sh tools/run.sh disks/disk4p-0`)` (xtrs on Xvfb :97, shell on the terminal; Ctrl+] quits).
