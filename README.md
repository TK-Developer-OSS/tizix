# tizix

A scratch-built, preemptive multitasking OS for Z80 and beyond (8-bit & 16-bit retro CPUs), engineered to thrive in just 32KB of RAM. Inspired by FUZIX, coded by AI.

It's a tiny UNIX-like OS for CPUs without an MMU. It runs on the Z80 and the 68000 — in simulators, and on real boards too.

**The star of the show: a real `vi`, running right on the machine.** A full-screen, modal editor (80×24) with insert and command modes, counted `dd` / `yy` and put, undo, and `:w` / `:q` / `:q!` / `:wq` / `:x`. On the Z80 it's compiled with our own C compiler and squeezed into just 12KB — three 4KB blocks — using overlays. Edit your files on an 8-bit machine, no host PC needed.

```
[/root]# ls /
bin/
root/
etc/
var/
dev/
[/root]# cat /etc/rc | wc -l
3
[/root]# a &
[/root]# ps
BLK ST CMD ARGS
0 rdy (init)
2 run sh
5 rdy a
```
(`a` is a little test program that keeps printing `A` in the background. We left its output out.)

## What it does

- **Real multitasking.** A timer interrupt switches between processes. Run things in the background with `cmd &`, check them with `ps`, stop them with `kill`, and hit Ctrl+C to stop whatever's in the foreground.
- **A proper shell.** `/bin/sh` is just a normal process that init starts. You get pipes (`A | B`, backed by a 4KB buffer in the kernel), `>` / `<` redirection, quoting, history with the arrow keys (saved in `/root/history`), and `cd` / `pwd`. It runs `/etc/rc` when it starts.
- **Files.** FAT (via FatFs) with a small VFS on top, plus a `/dev` with `null`, `fda` and `fdb`. You can read and write the raw disks with `dd`.
- **Commands.** Everything lives in `/bin`: `ls cat cp mv rm mkdir rmdir touch echo head tail wc grep sed uniq tee du dd date sleep vi rx telnet tzftp ntpdate rsyslog` and more. `vi` squeezes into three 4KB blocks thanks to overlays. `rx` pulls files in over XMODEM — handy for updating a board's SD card without pulling it out.
- **Networking** (z80pack). Start the `net &` daemon and `telnet` / `tzftp` can talk to TCP hosts through the simulator's socket extension.

## Where it runs

| ARCH | Target | Status |
|---|---|---|
| `z80pack` | z80pack's cpmsim (Z80, 64KB, floppy) | The default. The regression suite runs here |
| `z80board` | A homebrew Z80 board (32KB ROM + SD card, FT245 USB serial) | Works on the real board. The regression suite also runs on the z80boardsim simulator |
| `m68k-mega` | A bare MC68000 with an Arduino Mega2560 driving its bus (1MB SRAM, SD card) | Works on the real board (shell, pipes, background jobs, XMODEM). Simulator: m68ksim |
| `x86-ia16` | 16-bit x86 (qemu) | Has known bugs; not supported for now |

## How it's put together

- **Hardware stuff stays out of `src/`.** The kernel core in `src/` is shared by every target, and anything hardware-specific goes in `arch/<arch>/`. Fix something once and every target gets the fix.
- **Z80 programs can run anywhere in memory.** They're position-independent code based on the IY register, so they work wherever they land in the 4KB blocks — no relocation at load time. The Z80 commands are built with **tzcc** (`tzcc/`), our own **C compiler for the Z80**, which spits out IY-relative code directly. The Z80 kernel and a few commands use SDCC. On the 68000, gcc's `-mpcrel` does the same trick, so tzcc isn't used there.
- **No memory protection** — on purpose. Big programs fit into small slots by splitting into processes, sharing memory, and using overlays.

## Building

### What you need

We develop on Rocky Linux 9.

**For every target**: make, gcc (for the host; it builds tzcc and some helpers), python3, mtools, dosfstools (`mkfs.fat`).

| ARCH | To build | To run / test in a simulator | To use real hardware |
|---|---|---|---|
| `z80pack` | SDCC 4.5.0 (`sdcc`, `sdasz80`, `sdldz80`, `sdar`, `makebin`); tzcc gets built from `tzcc/` | `cpmsim` and `receive` from the z80pack fork | — |
| `z80board` | SDCC 4.5.0 (same as above) | `z80boardsim` from the z80pack fork | a ROM writer (32KB kernel ROM), `dd` for the SD card image |
| `m68k-mega` | m68k-elf binutils + gcc 8.3.0 (`m68k-elf-gcc`, `m68k-elf-ld`, `m68k-elf-objcopy`) | `m68ksim` (built from this tree; needs rocket68, see below) | the Arduino AVR toolchain (avr-gcc, avrdude) and Windows PowerShell for `arch/m68k-mega/arduino/`, `dd` for the SD card image |
| `x86-ia16` (unsupported) | ia16-elf-gcc 6.3.0 + binutils | qemu-system-i386 | — |

Two things you'll have to get yourself — they're not in this repo:

- **The z80pack fork**: `cpmsim`, `z80boardsim` and `receive` are built from a tizix-flavored z80pack 1.37. Drop them into `arch/z80pack/` and `arch/z80board/` (`make sims` does it for you if you ran configure with `--with-z80pack`). Note that `cpmsim` needs `receive` next to it or on your PATH.
- **[rocket68](https://github.com/habedi/rocket68)** (MIT): the 68000 CPU core inside m68ksim. Put it in `arch/m68k-mega/rocket68/`.

`./configure` checks all of this and tells you everything that's missing in one go. It only writes `config.mk` (where your tools are) and never touches the Makefiles. If you're working from a git checkout, run `autoconf` first to create `configure`.

```sh
./configure                           # look for tools on PATH
./configure --with-sdcc=/opt/sdcc-4.5.0/bin --with-m68k=/opt/m68k/bin --with-z80pack=../z80pack-tizix
make sims                             # build cpmsim / z80boardsim from the z80pack fork and put them in arch/
```

```sh
make                      # ARCH=z80pack (default): kernel, commands and the FAT disk image
make ARCH=z80board        # the z80board ROM image
make ARCH=m68k-mega       # m68k-mega: kernel, m68ksim and the disk image
```

### Docker

There's a Dockerfile in `docker/` (Ubuntu 24.04) with the whole toolchain baked in: the official SDCC 4.5.0 binaries, m68k-elf binutils 2.30 + gcc 8.3.0 built from pinned GNU sources, and the simulators. The tizix source isn't inside the image — just mount your tree at `/tizix`. Podman works too (add `:Z` to the mount on SELinux).

```sh
docker run --rm -v "$PWD":/tizix tizix              # build all three targets and run the regression suite
docker run --rm -v "$PWD":/tizix tizix make ARCH=z80board
docker run --rm -it -v "$PWD":/tizix tizix make run # boot z80pack and play with it
```

To build the image yourself (`sh docker/build.sh`) you'll need the z80pack fork and rocket68, since they're not in this repo.
Anything that touches real hardware (dd'ing an SD card, burning ROMs, flashing the Mega) happens on your host, not in the container.

## Running it

```sh
make run                          # z80pack: boot in cpmsim
make ARCH=z80board run            # z80board: boot in z80boardsim
make ARCH=m68k-mega run           # m68k-mega: boot in m68ksim (Ctrl+] to quit)
```

## Testing

```sh
sh python/run_regress.sh          # the regression suite: pty tests on z80pack (cpmsim), guards, tzcc runtime tests,
                                  #   then the z80board (z80boardsim) and m68k-mega (m68ksim) sections
sh python/run_regress.sh --clean  # start over with a fresh disk image
TIZIX_ARCH=z80board python3 python/test_args.py   # most tests run on z80board too
python3 python/test_args.py m68k-mega             # cross-target tests take the target as an argument
```

## What's where

| Path | What's inside |
|---|---|
| `src/` | The kernel every target shares (scheduler, kexec, VFS, pipes, FatFs) |
| `arch/<arch>/` | Per-target code (startup, console, disk, Makefile) |
| `arch/m68k-mega/arduino/` | Firmware for the Arduino Mega2560 that drives the 68000's bus |
| `user/` | Source for the commands, `sh` included |
| `tzcc/` | tzcc, our C compiler for the Z80 (it also has an x86-64 backend, used only to test itself with `make x86test`) |
| `python/` | Tests and debugging tools (`tz80.py` is a single-step Z80 debugger) |
| `docker/` | The Dockerfile and helper scripts |
| `DEVELOP.md` | Development notes and design discussions (in Japanese) |
| `task.md` | Our task tracker (in Japanese) |

## Things to watch out for

- File names play by FAT rules: **case doesn't matter**, and it's **8.3 names only** (long file names are turned off, `FF_USE_LFN 0`). `README.TXT` and `readme.txt` are the same file, and files you create on tizix end up in upper case (for example, `ls /root` shows `HISTORY`).
- A command line can be 48 characters at most.
- Only one pipe at a time, and at most 4KB goes through it (anything past that gets cut off).
- m68k-mega: the Mega firmware's timer (Timer5) has to tick at the same rate as the kernel's `TICK_HZ` (25 by default).

## Credits

- **TK** — project owner; wrote the very early code, and steers the design and the real-hardware work.
- **Google Gemini** (AI Mode)
- **Anthropic Claude** — Claude Sonnet 5, Claude Opus 5 and Claude Opus 5.5

## Thanks

tizix stands on the shoulders of others. Big thanks to:

- **[FatFs](https://elm-chan.org/fsw/ff/)** by ChaN — the FAT file system module tizix uses (`src/ff*`).
- **[z80pack](https://github.com/udo-munk/z80pack)** by Udo Munk — the Z80 simulator (cpmsim) our z80pack / z80board simulators are forked from.
- **[rocket68](https://github.com/habedi/rocket68)** by Hassan Abedi — the MC68000 CPU core inside m68ksim.
- **[SDCC](https://sdcc.sourceforge.net/)** — the Small Device C Compiler, for the Z80 kernel and tools.
- **[GCC](https://gcc.gnu.org/)** and **[GNU Binutils](https://www.gnu.org/software/binutils/)** — the m68k-elf toolchain and the host compiler.
- **[mtools](https://www.gnu.org/software/mtools/)** and **[dosfstools](https://github.com/dosfstools/dosfstools)** — for building the FAT disk images.
- **[Arduino](https://www.arduino.cc/)** (AVR core, avr-gcc, avrdude) — for the Mega2560 bus-host firmware.
- **[FUZIX](https://github.com/EtchedPixels/FUZIX)** — for the inspiration.

## License

MIT — see [LICENSE](LICENSE). FatFs (`src/ff*`) has its own 1-clause BSD-style license.
