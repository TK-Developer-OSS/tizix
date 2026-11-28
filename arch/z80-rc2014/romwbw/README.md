# tizix on the RC2014 (RomWBW)

The configuration this port runs on is the common RomWBW setup of an RC2014:

- a Z80 CPU module at 7.3728MHz
- the 512K RAM / 512K flash module with RomWBW (a Zed or Zed Pro)
- the Dual Clock module
- the SIO/2 serial module
- the Compact Flash module

## Build and run

```sh
make ARCH=z80-rc2014           # disks/cf.hd: a 4MB raw Compact Flash image
make ARCH=z80-rc2014 run       # MAME (rc2014zedp) with this terminal as the serial terminal
python3 python/test_z80_rc2014.py
```

`run` needs MAME 0.264 or later and `socat`. MAME also needs RomWBW 3.0.1's `RCZ80_std.rom` (`Binary/` in the RomWBW release package) on its rompath as `rc2014zedp/rcz80_std_3_0_1.rom`, for example `~/mame/roms/rc2014zedp/`. The ROM is not in this repository.

To boot:

1. At RomWBW's `Boot Selection?` prompt, type `2` (IDE0, the CF card).
2. Press Enter at the `Slice` prompt.

Ctrl+] leaves the terminal and stops MAME.

MAME runs without a window and without an X display. With a display, MAME (SDL) opens an empty full-screen window even with `-video none`, and the desktop looks frozen when that window is touched.

On a real RC2014, write `disks/cf.hd` to a CF card (for example with `dd`) and boot it the same way. This has not been tried yet.

## The timer: set CLOCK2 to 0.3072MHz

The RC2014 has no timer of its own, so tizix uses SIO/2 channel B as its tick:

- Channel B sends bytes to nothing, and its "transmitter empty" interrupt comes once a byte.
- Each byte takes 11 bits (8 data, 2 stop) at the channel's clock / 64.
- Channel B's clock is the Dual Clock module's CLOCK2. At 0.3072MHz that gives 436 interrupts a second.
- crt0.s counts every 7th one as a tick, which makes 62Hz (`TICK_HZ=62` in the Makefile).

On a real RC2014, CLOCK2 is set with its jumper. On MAME, `tools/run.sh` and the test set it with a config file. If CLOCK2 stays at 7.3728MHz, tizix still runs, but its clock runs 24 times too fast and the interrupts take about a tenth of the CPU.

## The card

The card holds one FAT12 volume (4MB, 1KB clusters) that starts at sector 0. Its reserved sectors hold:

| Sector | Contents |
|---|---|
| 2 | RomWBW's boot info: signature 0xA55A, load 0xA000-0xA200, entry 0xA000, label `tizix` |
| 3 | `boot.s`, which RomWBW loads to 0xA000 |
| 4-67 | the kernel, which `boot.s` loads to 0x0100 using the CF registers directly |

The load address is 0xA000 because RomWBW's loader runs at 0x8000, and its buffers and stack reach past 0x9000.
