# tizix

A scratch-built, preemptive multitasking OS for Z80 and beyond (8-bit & 16-bit retro CPUs), engineered to thrive in just 32KB of RAM. Inspired by FUZIX, coded by AI.

It's a tiny UNIX-like OS for CPUs without an MMU. It runs on the Z80, the 68000, the ESP32 and the Raspberry Pi Pico W — in simulators, and on real boards too.

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
- **ESP32** (`esp32-wroom-32e`). A single ESP32 runs the same kernel and commands off its own flash: built-in WiFi (DHCP, TCP, TLS, NTP), an MCP server (`mcpd`, so an AI client such as Claude Code can drive the board; see [doc/mcp.md](doc/mcp.md)), and commands for the chip's peripherals (`esp32-gpio`, I2C, SPI, ADC, PWM, RGB LED) through the `esp32d` helper daemon.

## Where it runs

| ARCH | Target | Status |
|---|---|---|
| `z80pack` | z80pack's cpmsim (Z80, 64KB, floppy) | The default. The regression suite runs here |
| `z80board` | A homebrew Z80 board (32KB ROM + SD card, FT245 USB serial) | Works on the real board. The regression suite also runs on the z80boardsim simulator |
| `z80-pc8801` | NEC PC-8801 (Z80, 5.25" floppy, text VRAM console) on the M88 emulator | Boots to the shell with the FAT disk, `vi` and the usual commands. Pipes are stubbed out for now (the kernel code area is tight). Not tried on real hardware. The regression suite (23 tests) runs on M88 under Wine |
| `m68k-mega` | A bare MC68000 with an Arduino Mega2560 driving its bus (1MB SRAM, SD card) | Works on the real board (shell, pipes, background jobs, XMODEM). Simulator: m68ksim |
| `m68k-x68000` | Sharp X68000 (MC68000, 1MB RAM minimum, 2HD floppy) on XM6G with a real IPL-ROM | Boots from a 2HD floppy image to the shell: FAT12 read/write, `vi`, pipes, background jobs and `kill`. Console and disk go through the ROM's IOCS, so no X68000 hardware is programmed directly. Not tried on real hardware yet. The regression suite (27 tests) runs on XM6G under Wine and reads the console from the emulator's memory |
| `msx` | MSX2 (Z80, 64KB RAM, 3.5" 2DD floppy with the machine's own disk ROM, 80-column text screen) on openMSX | Boots to the shell: FAT12 read/write, `vi`, `date` (the clock is set from the RP-5C01 at boot), pipes and background jobs. A 128KB-VRAM machine gets a sector cache in the spare VRAM, which is what keeps typing from dropping keys (see [the section below](#about-the-msx-port)). Not tried on real hardware yet |
| `esp32-wroom-32e` | A single ESP32 (ESP32-WROOM-32E, Xtensa LX6, 4MB flash with a FAT16 disk in it) | Boots on the real board: shell, FAT, WiFi, MCP server, GPIO and friends. The regression suite runs on Espressif's QEMU (WiFi is real-hardware only) |
| `rpi-pico` | Raspberry Pi Pico W (RP2040, dual Cortex-M0+, 264KB SRAM) with an optional SD card on SPI0 | Works on the real board: boots to the shell over the USB serial console. The root is the FAT SD card if one is present, otherwise a 1MB writable FAT in the Pico's own flash, so no SD card is required; `/dev/gpio` and `/dev/gpioN` drive the pins (`echo 1 > /dev/gpio5`, `cat /dev/gpio5`). Core 0 handles the hardware and the Wi-Fi chip, core 1 runs tizix. Only the original Pico W (RP2040) has been tried so far; more boards in the Pico family are planned. No simulator, no regression suite yet |
| `x86-ia16` | 16-bit real-mode x86 (386 or later; PC BIOS), QEMU and real hardware (VGA text screen + PS/2 keyboard; COM1 too if the machine has one) | Boots from a floppy, a hard disk or an SD card (BIOS INT 13h). The shell, `vi`, pipes and background jobs work. Process slots and 4KB memory blocks are sized from the memory found at boot. Tried on a ThinkPad R40 (USB floppy). The regression suite runs on QEMU (floppy and IDE-disk boot) |
| `x86-i386-unreal` | x86 PCs with a 386 or later and 4MB of memory or more, in unreal mode (real mode that can reach all 4GB; see [the section below](#about-the-x86-i386-unreal-port-unreal-mode)). QEMU and real hardware | Grew out of `x86-ia16`. The kernel's C is compiled by tzcc. Boots from a floppy; the shell, `vi`, pipes, XMODEM, an interrupt-driven serial driver, networking through an RS-232C modem (including `telnetd` for dial-in) and updating itself over the serial line work. Tried on a Dell PowerEdge T105 (USB floppy, 2GB). The regression suite runs on QEMU (128MB) and, over a serial cable, on the real machine |

## How it's put together

- **Hardware stuff stays out of `src/`.** The kernel core in `src/` is shared by every target, and anything hardware-specific goes in `arch/<arch>/`. Fix something once and every target gets the fix.
- **Z80 programs can run anywhere in memory.** They're position-independent code based on the IY register, so they work wherever they land in the 4KB blocks — no relocation at load time. The Z80 commands are built with **tzcc** (`tzcc/`), our own **C compiler for the Z80**, which spits out IY-relative code directly. The Z80 kernel and a few commands use SDCC. On the 68000, gcc's `-mpcrel` does the same trick, so tzcc isn't used there.
- **No memory protection** — on purpose. Big programs fit into small slots by splitting into processes, sharing memory, and using overlays.

## Try it first

If you just want to play with tizix, you don't need to clone anything or build anything — Docker is all it takes, and that includes Docker Desktop on Windows. The `tizix` image has prebuilt kernels, disk images and all three simulators. It doesn't include a compiler, so you can't `make` inside it. For that, see [Building](#building).

To keep your files and changes between sessions, run the container in the background with a specific name. You can safely access it using `docker exec`.

** NOTE.
This guide assumes that you already have**Docker (including Docker Desktop) installed on Windows 11**. 


**1. Start the container in the background:**
```sh
docker run -itd --name tizix tkdevelopeross/tizix:latest
```

**2. Log into the container as the `tizix` user:**
```sh
docker exec -it -u tizix tizix /bin/bash
```

**3. Manage the container lifecycle:**
```sh
docker stop tizix   # Stop the container without losing your data
docker start tizix  # Resume right where you left off
```

Once you're in as `tizix`, one command boots tizix:

| Command | What boots | To quit |
|---|---|---|
| `tizix` | z80pack (cpmsim, Z80) | Ctrl+\ |
| `tizix z80board` | the homebrew Z80 board (z80boardsim) | Ctrl+\ |
| `tizix m68k` | m68k-mega (m68ksim, 68000) | Ctrl+] |
| `tizix reset` | nothing — it puts the disk images back the way they shipped | — |

You get the shell prompt `[/root]#`. Some things to try: `ls /bin`, `echo hello | cat`, `sleep 30 &` then `ps` and `kill`, `vi memo.txt`, `cat /etc/rc`. Typing `exit` in sh won't close the simulator, because init just starts sh again, so use the quit keys above. Any files you change stay on the disk images until you run `tizix reset`. The source is in `~/tizix`, and `~/README.txt` has the same notes.


Cleanup / Uninstallation

If you want to completely remove `tizix` from your system, run the following commands:

**1. Stop and remove the container:**
```sh
docker stop tizix
docker rm tizix
```

**2. Remove the downloaded Docker image:**
```sh
docker rmi tkdevelopeross/tizix:latest
```



## Building

### What you need

We develop on Ubuntu 24.04 (the commands below assume it too).

**For every target**: make, gcc (for the host; it builds tzcc and some helpers), python3, mtools, dosfstools (`mkfs.fat`).

| ARCH | To build | To run / test in a simulator | To use real hardware |
|---|---|---|---|
| `z80pack` | SDCC 4.5.0 (`sdcc`, `sdasz80`, `sdldz80`, `sdar`, `makebin`); tzcc gets built from `tzcc/` | `cpmsim` and `receive` from the z80pack fork | — |
| `z80board` | SDCC 4.5.0 (same as above) | `z80boardsim` from the z80pack fork | a ROM writer (32KB kernel ROM), `dd` for the SD card image |
| `z80-pc8801` | SDCC 4.5.0 (same as above) | [M88](http://retropc.net/cisc/m88/) (a Windows PC-8801 emulator) under Wine + Xvfb, driven by `arch/z80-pc8801/tools/winerun.py`. The emulator and the PC-8801 ROM images aren't in this repo | — (not tried) |
| `msx` | SDCC 4.5.0 (same as above) | [openMSX](https://openmsx.org/) with the C-BIOS ROMs (`make ARCH=msx run`; the machine file and a stand-in disk ROM are built from `arch/msx/emu/`, no commercial ROM is needed) | a 2DD floppy drive (or a floppy emulator) for `disks/tizix.dsk` — not tried |
| `m68k-mega` | m68k-elf binutils + gcc 8.3.0 (`m68k-elf-gcc`, `m68k-elf-ld`, `m68k-elf-objcopy`) | `m68ksim` (built from this tree; needs rocket68, see below) | the Arduino AVR toolchain (avr-gcc, avrdude) and Windows PowerShell for `arch/m68k-mega/arduino/`, `dd` for the SD card image |
| `m68k-x68000` | m68k-elf binutils + gcc 8.3.0 (same as `m68k-mega`; this Makefile looks in `~/tools/m68k/bin`, or wherever `M68K=` points) | XM6G (XM6 Type G) under Wine + Xvfb, driven by `arch/m68k-x68000/tools/xm6run.py`. XM6G and the ROM images (`IPLROM.DAT`, `CGROM.DAT`) aren't in this repo | a 2HD floppy drive (or a floppy emulator) for the `.xdf` image |
| `esp32-wroom-32e` | Espressif's `xtensa-esp32-elf-gcc` (crosstool-NG, GCC 16.1.0) and `esptool`. For WiFi you also need Espressif's WiFi binaries and lwIP 2.2.0 (not in this repo; without them it builds without WiFi) | Espressif's QEMU, `qemu-system-xtensa -machine esp32` (`make -C arch/esp32-wroom-32e run`, Ctrl-A x to quit) | `esptool` to write `flash.bin` (4MB: kernel image at 0x1000, disk at 0x100000) to the module |
| `rpi-pico` | `arm-none-eabi-gcc`, CMake and the [Pico SDK](https://github.com/raspberrypi/pico-sdk) (point `PICO_SDK_PATH` at it). The SDK headers are needed for the commands too, not just the kernel | — (none yet) | an SD card module on SPI0 is optional (MISO GP16, CS GP17, SCK GP18, MOSI GP19); copy `kernel.uf2` onto the Pico in BOOTSEL mode, write `sdcard.img` to the card with `dd` or over XMODEM (see below) |
| `x86-ia16` | ia16-elf-gcc 6.3.0 + binutils, mtools, dosfstools | `qemu-system-i386` (`make ARCH=x86-ia16 clean all run`, Ctrl-] to quit) | a USB floppy drive (or an SD card / USB stick) to `dd` `disk/floppy.img` onto, and a BIOS-boot PC |
| `x86-i386-unreal` | ia16-elf-gcc 6.3.0 + binutils (preprocessor, assembler, linker, and the commands), tzcc (built from `tzcc/`) for the kernel, mtools, dosfstools | `qemu-system-i386 -m 128` (`arch/x86-i386-unreal/tz/check.sh` builds and boots the tzcc kernel) | a USB floppy drive to `dd` `disk/floppy_tz.img` onto and a BIOS-boot PC with a 386 or later and 4MB+. For networking: a serial cable and a Hayes-style modem (a Raspberry Pi with a USB serial adapter running `python/at_modem.py --serial` stands in for a WiFi modem); lrzsz (`sx`) for sending files by XMODEM |

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
make ARCH=rpi-pico        # rpi-pico: kernel.uf2 (copied to the top of the tree) and arch/rpi-pico/sdcard.img
make -C arch/x86-i386-unreal -f Makefile.tz OBJ=obj/tz IMG=disk/floppy_tz.img   # x86-i386-unreal: tzcc kernel + commands
```

### Hint: installing the build tools

How we set each target up. The table above says *what* is needed; this is *how*. Paths are only examples — `./configure` takes `--with-sdcc=` / `--with-m68k=` and writes them to `config.mk`, so anywhere on disk works.

**Common (every target)**

```sh
sudo apt install build-essential python3 mtools dosfstools
```

**`z80pack`, `z80board`, `z80-pc8801`: SDCC 4.5.0**

Use the official binary release, not the distro package. Ubuntu's `sdcc` is 4.2.0, whose assembler doesn't understand `.ifdef`, and the build breaks.

```sh
curl -L -o sdcc.tar.bz2 \
  https://sourceforge.net/projects/sdcc/files/sdcc-linux-amd64/4.5.0/sdcc-4.5.0-amd64-unknown-linux2.5.tar.bz2/download
sudo mkdir -p /opt/sdcc-4.5.0 && sudo tar xjf sdcc.tar.bz2 -C /opt/sdcc-4.5.0 --strip-components=1
export PATH=/opt/sdcc-4.5.0/bin:$PATH      # put it on PATH; a symlink in /usr/local/bin won't do (sdcc can't find its headers)
```

The simulators come from the z80pack fork: `./configure --with-sdcc=/opt/sdcc-4.5.0/bin --with-z80pack=../z80pack-tizix`, then `make sims`.

**`m68k-mega`: m68k-elf binutils 2.30 + gcc 8.3.0**

Nothing recent is packaged for this, so build it from source (the Dockerfile has the exact flags and checksums; we build it with `gcc-11` rather than Ubuntu 24.04's default gcc 13):

```sh
sudo apt install gcc-11 g++-11 bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev
# binutils-2.30.tar.gz, then:
CC=gcc-11 CXX=g++-11 ../binutils-2.30/configure --target=m68k-elf --prefix=/opt/m68k --disable-nls --disable-werror
make -j"$(nproc)" && sudo make install
# gcc-8.3.0.tar.gz, then (PATH must already contain /opt/m68k/bin):
CC=gcc-11 CXX=g++-11 ../gcc-8.3.0/configure --target=m68k-elf --prefix=/opt/m68k --disable-nls --disable-threads \
    --disable-shared --disable-libssp --enable-languages=c --without-headers --with-newlib --disable-werror
make -j"$(nproc)" all-gcc all-target-libgcc && sudo make install-gcc install-target-libgcc
```

Then put [rocket68](https://github.com/habedi/rocket68) in `arch/m68k-mega/rocket68/`; `make ARCH=m68k-mega` builds `m68ksim` from it. The Arduino side (`arch/m68k-mega/arduino/`) needs avr-gcc and avrdude.

**`esp32-wroom-32e`: Espressif toolchain, QEMU and esptool**

- `xtensa-esp32-elf-gcc`: Espressif's crosstool-NG release (we use GCC 16.1.0). Unpack it and put its `bin/` on PATH. The Makefile calls `xtensa-esp32-elf-gcc`, so if your release names the binary `xtensa-esp-elf-gcc`, put a symlink under the old name.
- `qemu-system-xtensa`: Espressif's QEMU fork (`-machine esp32`); stock QEMU won't do. Only needed for `run` and the regression suite.
- `esptool`: `pip install esptool` (or `pipx`). Used to make the boot image and to write the flash.
- WiFi is optional. Without the blobs the build just goes ahead without WiFi (and the QEMU regression still runs). For WiFi, put Espressif's prebuilt WiFi libraries and ESP-IDF headers (from Arduino-ESP32 3.3.2) in `~/tools/esp32-wifi-blobs` and lwIP 2.2.0 in `~/tools/lwip-2.2.0`; override with `WIFI_BLOBS=` / `LWIP_DIR=`, or pass `WIFI=0` to switch it off.

**`rpi-pico`: arm-none-eabi-gcc, CMake and the Pico SDK**

```sh
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk ~/tools/pico-sdk
export PICO_SDK_PATH=$HOME/tools/pico-sdk
make ARCH=rpi-pico
```

`--recurse-submodules` matters: TinyUSB (the USB serial console), the CYW43 Wi-Fi driver and lwIP are submodules of the SDK. The commands are built from the SDK's headers too, so there's no building them without the SDK. The SDK version we built against isn't pinned yet.

**`z80-pc8801`, running the tests: Wine + Xvfb**

```sh
sudo apt install wine xvfb xdotool imagemagick
```

Download M88 (the "M88p2 for Windows" build) from [retropc.net/cisc/m88](http://retropc.net/cisc/m88/) and unpack it; it's a Windows program and Wine runs it as it is. The harness looks for everything in one fixed directory, `~/work/pc88` (set at the top of `winerun.py` if you want it elsewhere):

```
~/work/pc88/
├── M88.exe      from the M88 archive
├── N88.ROM      32KB  } the ROM images M88 loads from its own directory
├── DISK.ROM     2KB   } (not in this repo, and not ours to hand out; bring your own)
└── FONT.ROM     2KB   } `tools/mkfont.py` builds one from a Windows bitmap font (the paths in it are hard-coded for Windows; edit them), or bring your own
```

You don't need `M88.ini`; M88 writes one itself. (We once hit a page fault with a hand-placed one, cause unknown, so leave it out.) `winerun.py boot` copies the disk images from `arch/z80-pc8801/disks/` into this directory as `tizix_a.d88` / `tizix_b.d88` every time, so those two files are overwritten. `winerun.py` uses podman if it's there and plain host Wine if not; either way it draws on a virtual display (`:99`) and never touches your real screen.

**`m68k-x68000`, running the tests: Wine + Xvfb**

```sh
sudo apt install wine xvfb xdotool imagemagick mtools dosfstools
```

XM6G (the Type G build of XM6) is a Windows program that Wine runs as it is. The harness looks for it in `~/tools/xm6/` (set `XM6_DIR` to change that), together with two ROM images that are not in this repo and not ours to hand out:

```
~/tools/xm6/
├── xm6g.exe
├── IPLROM.DAT   the X68000 IPL-ROM (128KB)
└── CGROM.DAT    the character generator ROM
```

XM6G shows a compatibility warning when the CGROM is not a genuine one; the harness clicks "continue" for you. `xm6run.py` starts XM6G on its own virtual display (`:98`, `X68K_DISPLAY` to change) and never touches your real screen, keyboard or mouse. It doesn't read the screen either: the kernel keeps a copy of everything it prints in a 4KB ring at `$0FF000`, and the script reads that ring out of the XM6G process memory (read-only). Wine must be able to open `/proc/<pid>/mem`, which is the case when the harness started XM6G itself.

**Where the images for real hardware end up**

Paths are from the top of the tree, after `make ARCH=<arch>`. `build/` and the images are git-ignored, so they're never in the checkout; you always build them.

| ARCH | What you write | Where it is | How it goes onto the board |
|---|---|---|---|
| `z80board` | kernel ROM (32KB) | `arch/z80board/boot.rom` | a ROM writer, into the board's ROM |
| | SD card image (4MB) | `arch/z80board/sdcard.img` | `dd` to the whole SD device (Rufus "DD image" on Windows); `make ARCH=z80board flash` prints the same steps |
| `m68k-mega` | kernel, as a C array for the Arduino sketch | `arch/m68k-mega/rom.h`, copied into `arch/m68k-mega/arduino/MEGA2560_68000_DTACK_TEST/` | build and upload that sketch to the Mega2560 (the kernel lives in the sketch, not on the card) |
| | SD card image (32MB, optional) | `build/arch/m68k-mega/obj/disk.img` | `dd` to the SD device |
| `m68k-x68000` | 2HD floppy image (1,261,568 bytes, FAT12, 1024-byte sectors) | `build/arch/m68k-x68000/obj/disk.xdf` | open it in XM6G's FDD0, or write it to a real 2HD disk (the boot sector is a normal X68000 one, so any tool that writes `.xdf` images will do) |
| `esp32-wroom-32e` | the whole 4MB flash (kernel at 0x1000, FAT disk at 0x100000) | `build/arch/esp32-wroom-32e/obj/flash.bin` | `esptool --chip esp32 -p /dev/ttyUSB0 write-flash 0x0 flash.bin` (the Makefile's `make ARCH=esp32-wroom-32e flash PORT=...` writes the same pieces, but is marked untested there) |
| `rpi-pico` | firmware | `kernel.uf2` at the top of the tree (copied from `arch/rpi-pico/build/`) | BOOTSEL, then copy it to the Pico's drive |
| | SD card image (32MB) | `arch/rpi-pico/sdcard.img` | `dd` to the SD device, or from the running Pico: press `x` in the 16-second boot countdown and send it by XMODEM |

Before you `dd` anything, check the device name with `lsblk`. A wrong `of=` wipes a disk.

**Check it**

`./configure` lists everything that's missing in one go, and `sh python/run_regress.sh` skips (and says why) any target whose tools it can't find, so a partly-installed machine still runs the parts it can.

### Docker

There's a Dockerfile in `docker/` (Ubuntu 24.04) with the whole toolchain baked in: the official SDCC 4.5.0 binaries, m68k-elf binutils 2.30 + gcc 8.3.0 built from pinned GNU sources, and the simulators. The tizix source isn't inside the image — just mount your tree at `/tizix`. Podman works too (add `:Z` to the mount on SELinux).

#### build all three targets and run the regression suite
```sh
docker run --rm -v "$PWD":/tizix tizix
docker run --rm -v "$PWD":/tizix tizix make ARCH=z80board
docker run --rm -it -v "$PWD":/tizix tizix make run # boot z80pack and play with it
```

To build the image yourself (`sh docker/build.sh`) you'll need the z80pack fork and rocket68, since they're not in this repo.
Anything that touches real hardware (dd'ing an SD card, burning ROMs, flashing the Mega) happens on your host, not in the container.

## Running it

```sh
make run                          # z80pack: boot in cpmsim
make ARCH=z80board run            # z80board: boot in z80boardsim
(cd arch/z80-pc8801/tools && python3 -u winerun.py boot)   # z80-pc8801: boot in M88 under Wine + Xvfb (after make ARCH=z80-pc8801; see Testing)
make ARCH=msx run                 # msx: boot in openMSX (after make ARCH=msx)
make ARCH=m68k-mega run           # m68k-mega: boot in m68ksim (Ctrl+] to quit)
make ARCH=esp32-wroom-32e run     # esp32: boot in Espressif's QEMU (Ctrl-A x to quit)
```

**Raspberry Pi Pico W** (`rpi-pico`) has no simulator; it runs on the real board. Hold BOOTSEL while plugging it in and copy `kernel.uf2` to the drive that shows up. The console is the Pico's USB serial port (USB CDC; any terminal program will do). After boot there's a 16-second countdown: press `x` during it to enter the XMODEM SD flasher, then send `sdcard.img` with XMODEM (CRC) from the terminal. It writes the image raw to the SD card from sector 0 and reboots, so you can update the card without pulling it out.

Without an SD card the root is a FAT image inside the firmware (`/bin`, `/etc`, `/root`, ... built by `arch/rpi-pico/tools/mkflashfs.sh`), writable and kept across reboots; flashing a new `kernel.uf2` resets it. Commands that can't run as separate programs on this port yet (`ls`, `cat`, `echo`, `cp`, `mv`, `rm`, `mkdir`, `touch`, `df`, `sync`, `uptime`) are built into the kernel. GPIO 16-19 (the SD card's SPI) and 23/24/25/29 (the Wi-Fi chip) are refused. With `picotool` installed, `picotool load -f -x kernel.uf2` flashes the board without pressing BOOTSEL.

## Testing

```sh
sh python/run_regress.sh          # the regression suite: pty tests on z80pack (cpmsim), guards, tzcc runtime tests,
                                  #   then the z80board (z80boardsim), m68k-mega (m68ksim) and esp32-wroom-32e (QEMU) sections
                                  #   (REGRESS_SKIP_ESP32=1 skips the ESP32 one)
sh python/run_regress.sh --clean  # start over with a fresh disk image
TIZIX_ARCH=z80board python3 python/test_args.py   # most tests run on z80board too
python3 python/test_args.py m68k-mega             # cross-target tests take the target as an argument
```

PC-8801 has its own harness (it needs `wine` and `xvfb`, plus `M88.exe` and the ROM images in `~/work/pc88`):

```sh
make ARCH=z80-pc8801
cd arch/z80-pc8801/tools
python3 -u winerun.py boot                    # boot tizix on M88 (about a minute)
python3 -u winerun.py tests pc88_tests.txt    # RESULT: n / m PASS (23 / 23 at the moment)
```

X68000 (XM6G and the ROM images in `~/tools/xm6`):

```sh
make ARCH=m68k-x68000
python3 -u arch/m68k-x68000/tools/xm6run.py tests arch/m68k-x68000/tools/x68k_tests.txt   # n passed, 0 failed
```

tizix-mcu / mcu-low (no emulator of the whole system; the Uno test needs the board, the PIC tests need `gputils` and `gpsim`):

```sh
make ARCH=avr-uno test      # Uno: serial smoke test against the real board on /dev/ttyACM0 (close the terminal first)
make ARCH=mcu-low test      # PIC12F683, nothing but the PC: the reference machine, the tzsh converter and the host command
                            #   (mcu-low/vm/test_vm.py, mcu-low/host/test_tzlow.py), then the firmware in gpsim:
                            #   vm_test.py (programs run), node_test.py (the bare link), link_test.py (link + machine together)
make ARCH=mcu-low linktest  # only the link + machine test
```

Each prints `ok` / `FAIL` lines and `0 failure(s)` at the end. A new bytecode instruction goes into `mcu-low/vm/nodevm.py` (reference) first, then `vm.asm`, and the same program must give the same pin edges in both.

## What's where

| Path | What's inside |
|---|---|
| `src/` | The kernel every target shares (scheduler, kexec, VFS, pipes, FatFs) |
| `arch/<arch>/` | Per-target code (startup, console, disk, Makefile) |
| `arch/m68k-x68000/` | Sharp X68000 port (boot sector, IOCS console and disk, tests, `XM6.md`) |
| `arch/m68k-mega/arduino/` | Firmware for the Arduino Mega2560 that drives the 68000's bus |
| `arch/rpi-pico/` | Raspberry Pi Pico W port. The kernel is built with CMake against the Pico SDK (`CMakeLists.txt`); the Makefile drives that and builds the commands and the SD card image |
| `user/` | Source for the commands, `sh` included |
| `tzcc/` | tzcc, our C compiler for the Z80 (it also has an x86-64 backend, used only to test itself with `make x86test`) |
| `python/` | Tests and debugging tools (`tz80.py` is a single-step Z80 debugger) |
| `docker/` | The Dockerfile and helper scripts |

## Things to watch out for

- File names play by FAT rules: **case doesn't matter**, and it's **8.3 names only** (long file names are turned off, `FF_USE_LFN 0`). `README.TXT` and `readme.txt` are the same file, and files you create on tizix end up in upper case (for example, `ls /root` shows `HISTORY`).
- A command line can be 48 characters at most.
- Only one pipe at a time, and at most 4KB goes through it (anything past that gets cut off).
- m68k-x68000: it needs a 1MB X68000 at least, and it relies on the ROM's IOCS (console, floppy, and the 100Hz Timer-C tick), so it only boots from FDD0 with the IPL-ROM present. Details, the boot sequence and the IOCS facts we verified are in `arch/m68k-x68000/XM6.md`. Only FDD0 is a FAT volume: no hard disk, no RTC (the clock starts at 1970-01-01), RAM above 1MB is unused. It has only been run on XM6G, never on a real X68000. Other emulators (XEiJ, MAME, the MiSTer core) haven't been tried; reports are welcome. tizix doesn't use Human68k at all: it boots straight from the floppy's boot sector and talks to the ROM's IOCS, so Human68k programs won't run on it and it isn't a Human68k replacement.
- m68k-mega: the Mega firmware's timer (Timer5) has to tick at the same rate as the kernel's `TICK_HZ` (25 by default).

## Credits

- **TK** — project owner; wrote the very early code, and steers the design and the real-hardware work.
            @TK_Developer  https://x.com/TK_Developer

- **Google Gemini** (AI Mode)
- **Anthropic Claude** — Claude Sonnet 5, Claude Opus 5 and Claude Opus 5.5

## Thanks

tizix stands on the shoulders of others. Big thanks to:

- **[FatFs](https://elm-chan.org/fsw/ff/)** by ChaN — the FAT file system module tizix uses (`src/ff*`).
- **[z80pack](https://github.com/udo-munk/z80pack)** by Udo Munk — the Z80 simulator (cpmsim) our z80pack / z80board simulators are forked from.
- **[rocket68](https://github.com/habedi/rocket68)** by Hassan Abedi — the MC68000 CPU core inside m68ksim.
- **[M88](http://retropc.net/cisc/m88/)** by cisc — the PC-8801 emulator the `z80-pc8801` port is developed and tested on.
- **j80** (PC-8000 series emulator) by Shigekazu HIRAO (HAL8999) — we used it for our PC-8001 experiments (a ROM boot loader, the PC-8012-02 RAM board).
- **XM6** by PI. (2001-2006, and again since 2022) and **XM6 TypeG** by GIMONS — the X68000 emulator the `m68k-x68000` port is developed and tested on. PI. also published the XM6 source, which we read to check how the FDC and the disk behave.
- **[QEMU](https://www.qemu.org/)** and Espressif's fork of it (`qemu-system-xtensa -machine esp32`) — the simulator behind the `esp32-wroom-32e` regression tests; the plain QEMU also runs the `x86-ia16` ones.
- **[SDCC](https://sdcc.sourceforge.net/)** — the Small Device C Compiler, for the Z80 kernel and tools.
- **[GCC](https://gcc.gnu.org/)** and **[GNU Binutils](https://www.gnu.org/software/binutils/)** — the m68k-elf toolchain and the host compiler.
- **[mtools](https://www.gnu.org/software/mtools/)** and **[dosfstools](https://github.com/dosfstools/dosfstools)** — for building the FAT disk images.
- **[Arduino](https://www.arduino.cc/)** (AVR core, avr-gcc, avrdude) — for the Mega2560 bus-host firmware.
- **[FUZIX](https://github.com/EtchedPixels/FUZIX)** — for the inspiration.

## About the msx port

MSX2 with 64KB of RAM and a 3.5" 2DD floppy (720KB, FAT12). It uses the machine's own disk ROM (DSKIO) for the disk and the BIOS only to set up the 80-column text screen, so it needs a disk interface in the machine and nothing else. `make ARCH=msx` builds `arch/msx/msx2-64kb-2dd/disks/tizix.dsk`; `make ARCH=msx run` starts openMSX with it.

- **Boot.** The boot sector loads `s2` (sector 1), which installs a small disk trampoline at 0x8C78, reads the kernel (sectors 2-65) through it into RAM below 0x8000 and jumps in. The trampoline switches pages 0 and 1 to the BIOS ROM and the disk ROM for each DSKIO call and back to RAM afterwards, so the kernel keeps all 64KB.
- **Console.** The name table of SCREEN 0 is written straight through the VDP ports. Keys are scanned from the vblank interrupt (60Hz) out of the keyboard matrix; the cursor keys are turned into `ESC [ A/B/C/D` so `vi` and the shell history work. No key repeat, no kana.
- **Why there is a sector cache.** The disk ROM keeps interrupts off for the whole of a DSKIO call (0.2-0.3 s for even one sector), and a key pressed and released inside that window is never seen. A command load is 8-10 such calls, so keys were dropped badly after `vi`, `date` and `:`. On a machine with 128KB of VRAM (MSX2 normally has it) the 80-column text screen leaves about 96KB of VRAM unused; tizix keeps recently read sectors there (192 slots, write-through, tags at 0x07000). A sector that is in the cache is copied from VRAM in a few milliseconds without calling the disk ROM at all. The first run of a command still loads from the disk; later runs do not. With 64KB of VRAM the cache switches itself off and everything works as before, just with the drops.
- **Limits.** The cache doesn't notice a floppy being swapped; reboot after changing disks. 50Hz machines run the clock 17% slow (`TICK_HZ` is 60). Only MSX2/2+ is the target (MSX1 later; the R800 of the turboR isn't one). Real hardware hasn't been tried.
- **`vi`.** The first `:` after starting `vi` still loads the `:`-line overlay from the disk (interrupts are off for about 0.4 s), so type the rest of the command after a moment; the second `:` and everything after is served from the cache.

## About the x86-ia16 port

A real-mode (16-bit) port for PCs with a BIOS. It is the one that grew out of a "what is the smallest thing that still looks like tizix" question, and it keeps using the shared kernel in `src/`.

- **Console.** Output goes to the VGA text screen and to COM1 (the 16550 is probed at boot and skipped when it isn't there). Input comes from the PS/2 keyboard (read straight from the 8042, Japanese JIS layout table) or from COM1. ANSI escapes (`ESC[2J`, `ESC[r;cH`, `ESC[K` ...) are interpreted, so `vi` works on the screen.
- **Boot.** `stage1` (the boot sector) loads `stage2`, which loads the kernel. Drive 0x00 is read with CHS; drive 0x80 and up (hard disk, USB stick, SD card) with the INT 13h extensions. Put the 1.44MB image at the start of the medium, no partition table.
- **Memory.** The conventional memory above the kernel is a pool of 4KB blocks; a command takes as many blocks as its image, bss and 2KB of stack need. The number of blocks and of process slots (up to 24) follows the memory the BIOS reports. `free` shows both. Memory above 1MB is reported but not used (a plain 8086 real-mode program cannot reach it).
- **Things to know.** Assembly routines must save and restore ES/DS (ia16-gcc keeps pointers in them across calls). The kernel has a per-process syscall stack, which is what makes pipes work.
- **Networking (experimental).** `netpoc` talks to an NE2000 under QEMU's user-mode NAT (`-netdev user -device ne2k_isa,iobase=0x300`): ARP and ICMP ping work. A TCP/IP stack for the shared `curl`, `nc` and `dig` is planned.
- Design notes, the verification report and the real-hardware notes are in `arch/x86-ia16/diff/` and `arch/x86-ia16/REALHW.md`.

## About the x86-i386-unreal port (unreal mode)

Why put tizix on an x86 PC at all, when Linux and the BSDs run there just fine? Two reasons:

- **A hobby: a second life for old PCs.** Plenty of PCs have no job left. Giving one a small system you can read from end to end, and having fun with it, is the point.
- **Groundwork.** It is a stepping stone toward an x86 kernel that we plan to develop on a separate line later.

**What unreal mode is.** On a 386 or later, the CPU keeps a hidden limit for each segment register. Switch to protected mode for a moment, load DS/ES/FS/GS with 4GB-limit descriptors, and come straight back to real mode: the segments still have the 4GB limit. The machine is in plain real mode again (BIOS calls, 16-bit code, the usual segment:offset), but a 32-bit offset can now reach any byte of the first 4GB. It is not an official CPU mode, but every 386 and later behaves this way. `arch/x86-i386-unreal/xmem.s` sets it up at boot and checks it with a write/read round trip above 1MB; the boot banner says `unreal mode: ok`.

**What this port has today.**

- **Floor: a 386 and 4MB.** The kernel stops with a message on anything smaller. QEMU runs it with 128MB; the Dell PowerEdge T105 we use as the real-hardware bench has 2GB, all of it visible to unreal mode.
- **The kernel's C is compiled by tzcc**, our own compiler (its 16-bit x86 backend), not by ia16-gcc. ia16-gcc assumes the stack and data segments are the same (SS = DS), which is a dead end for what comes next: putting a process's data above 1MB is planned as a tzcc memory model for unreal mode. The commands are still built with ia16-gcc.
- **High memory today**: `mmio` reads and writes any physical address (PCI device registers near 4GB included; `lspci -v` lists them). Ordinary programs cannot keep their own data above 1MB yet.
- **Serial**: an interrupt-driven 16550 driver for COM1 and COM2 with receive buffers, so waiting programs sleep instead of spinning, and a paste does not lose bytes. The console and the modem share it; `sercon off` hands COM1 over to a modem on a machine with one serial port.
- **Networking through an RS-232C modem**, the way a PC of that era would: `ntpdate`, `curl`, `telnet` dial out with `ATDT host:port`, and `telnetd` answers incoming calls (`RING` / `ATA`) and shares the console with the caller. A Raspberry Pi with a USB serial adapter running `python/at_modem.py --serial /dev/ttyUSB0 --listen 2323` stands in for a WiFi modem.
- **Looking after it from a distance**: files go over the serial line by XMODEM (`rx`), `sysup` rewrites the boot area (boot sector, stage2, kernel) after checking the disk format, and `reboot` restarts through INT 19h without going through the BIOS POST (a machine with a dead CMOS battery would otherwise wait for F1). `arch/x86-i386-unreal/tools/t105_update.py` puts this together: send what changed, rewrite the kernel, reboot, wait for the prompt. `tools/test_interactive.py` runs the same regression suite on QEMU or, with `TIZIX_SERIAL=/dev/ttyS0`, on the real machine over the cable.

## About tizix-mcu (microcontrollers)

A separate, tiny line of tizix for microcontrollers, in [`mcu/`](mcu). It shares no source with `src/`: there is no kernel, no file system and no process model, only a shell, a few builtin commands, a script interpreter and the peripherals as nodes under `/dev`. The first target is the Arduino Uno / Nano (ATmega328P: 32KB flash, 2KB RAM). The point is the same skill everywhere: devices hang under `/dev`, and `echo 1 > /dev/gpio/13` works on the Uno the way it will on a PIC and on the main tizix. Details: [mcu/README.md](mcu/README.md).

**Why it exists.** A script is longer than the same thing in C or assembler, and that is the price: in return the same script means the same thing on every chip (an interpreter on the Uno, compact bytecode on a PIC, `tzsh` on the main tizix), so nothing has to be rewritten per part. The smallest parts (PIC12F class, DIP-8) are a separate line, [`mcu-low/`](mcu-low) (see below): bytecode in EEPROM, a tiny virtual machine in assembler, and a host command that makes them look like tizix.

A feature joins the language only if every target can express it or a `config.h` switch can leave it out.

**Build, write, talk to it** (an Uno on `/dev/ttyACM0`; `PORT=` changes it; needs `avr-gcc`, `avrdude`, `picocom`):

```
make ARCH=avr-uno             # build, with the flash size (limit 32256 bytes, under Optiboot)
make ARCH=avr-uno flash       # write it through the Optiboot bootloader
make ARCH=avr-uno run         # flash, then open the console (leave picocom with Ctrl-A Ctrl-X)
make ARCH=avr-uno test        # serial smoke test against the real board
```

Only one program can hold the serial port: close the terminal before `flash` or `test`.

**What you see at the prompt.**

```
[/]# ls                      # everything, recursively, in columns: /bin/…  /blink.sh  /dev/gpio/2 …
[/]# cat /blink.sh           # a baked-in script's text
[/]# echo 1 > /dev/gpio/13   # drive pin D13 (0, 1, in, up); cat /dev/gpio/13 reads it
[/]# echo 500 > /dev/timer/0/limit    # timer 0: limit (ms), finish (flag), repeat (0 one-shot, 1 periodic)
[/]# tzsh blink.sh           # run a script (also tzsh /blink.sh); Ctrl-C stops it
```

**The script language (`tzsh`).** C-like control around shell-like commands, values are strings (numbers are 16 bit, only where you add or compare):

```
setup { echo 1 > /dev/timer/0/repeat; echo 500 > /dev/timer/0/limit; }   // once
intr  { echo 0 > /dev/timer/0/finish; if (/dev/gpio/13 == "0") { echo 1 > /dev/gpio/13; } else { echo 0 > /dev/gpio/13; } }
loop  { n = n + 1; if (n == 1000) { echo "1000 rounds"; n = 0; } }       // forever; write it last
```

`intr` runs between statements after the timer finishes (the interrupt itself only raises `/dev/timer/0/finish`). `while`, `if / else if / else`, `break`, `exit`, `NAME = value [+|- value]` and `> /dev/...` are the rest. The source is [user/tzsh/tzsh_core.c](user/tzsh/tzsh_core.c); [user/tzsh/conformance/mcu.sh](user/tzsh/conformance/mcu.sh) is its self-test, run on the board by `make ARCH=avr-uno test`.

**Your own scripts.** Put a `.sh` file in `mcu/scripts/` and add it to `SCRIPT_FILES` in `mcu/avr-uno/Makefile`; it is baked into flash under its file name and shows up in `ls` as `/NAME.sh`. (Sending a script to the board without rebuilding is planned, task #363.)

**Configuration (planned).** Today every feature is built in: about 17KB of the 32KB flash and 564 bytes of RAM. To reach smaller parts (PIC16/12 class), each target will get a `config.h` that switches features on and off with `#define`s (the shell, `ls`/`cat`, the interpreter, `intr`, background jobs, a log) and sets limits (`NVARS`, `VALLEN`); a disabled feature removes its code, tables and RAM. A menuconfig-style TUI that writes `config.h` is planned after that (tasks #365, #367). Background jobs with a boot-time `run.sh` are task #364, and a ring log is #366.

## About tizix-mcu-low (PIC12F class)

For parts with no room for a shell or an interpreter (RAM of a hundred bytes, an 8-deep stack), in [`mcu-low/`](mcu-low). The script is compiled on the PC to **4-bit-opcode bytecode** (one byte per instruction: the upper nibble is the operation, the lower the operand), stored in the chip's **EEPROM** and run by a small **virtual machine written in assembler**. The same `.sh` that runs on the Uno (a subset of `tzsh`: `setup / intr / loop`, `echo` to `/dev/gpio/N` and `/dev/timer/0/*`, `if / else`, counters, messages) turns into 17 to 51 bytes for a blink. The first target is the **PIC12F683** (the PIC16F84A is next); parts without EEPROM are out.

- **One wire.** The host talks to the chip over a single pin (GP2), 500 baud 8N1, half duplex: the host is master, a frame is a command byte (bit 7 = write, low 7 bits = node) plus a value byte for a write, and the chip answers with exactly one byte. Nodes 0 to 6 are the machine's bit nodes (`gpio/0,1,4,5,3`, `timer/0/finish`, `timer/0/repeat`); `0x20/0x21` is the EEPROM pointer and data, `0x22` the output ring, `0x23` stop / restart.
- **Size.** `vm.asm` (machine and link in one firmware) is 790 of 2048 words; the A/D converter is not implemented yet.
- **Host command.** [`mcu-low/host/tzlow.py`](mcu-low/host/tzlow.py) (Python, pyserial): `ls`, `cat PATH`, `echo V > PATH`, `load FILE.sh|.s` (compile, write to EEPROM, read back, restart), `dump`, `stop`, `start`, and a `[pic]# ` prompt when run with no command. The wiring: USB serial RX on the line, TX through 1k to 4.7k onto the line (the host hears its own bytes and skips them). Writing the firmware to a real chip is not wired up yet (`make flash` is a placeholder; no PICkit2 tool here), and the host has not been tried on hardware.

```
make ARCH=mcu-low                          # assemble the machine (vm), the bare link (node) and the first blink; prints words used
make ARCH=mcu-low DEVICE=pic12f683 size    # DEVICE= picks the chip (a directory in mcu-low/)
make ARCH=mcu-low run                      # the host command against a pretend device on this PC (no hardware)
make ARCH=mcu-low run PORT=/dev/ttyUSB0    # ... or against a chip on the link
```

## License

MIT — see [LICENSE](LICENSE). FatFs (`src/ff*`) has its own 1-clause BSD-style license.
