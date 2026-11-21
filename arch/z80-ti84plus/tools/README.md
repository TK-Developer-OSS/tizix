# arch/z80-ti84plus tools

`tizsim.c` is a small frontend of [z80e](https://github.com/KnightOS/z80e) (MIT, the KnightOS emulator of TI
calculators) that runs `rom.bin` and connects the **link assist** of the calculator (the hardware it uses to talk
to a PC) to stdin / stdout. That is the console of tizix on this port.

    make ARCH=z80-ti84plus                         # builds arch/z80-ti84plus/rom.bin (kernel + FAT image)
    printf 'ls /bin\r' | /mnt/sda1/ti84/tizsim arch/z80-ti84plus/rom.bin     # feed lines, read the output
    /mnt/sda1/ti84/tizsim -r arch/z80-ti84plus/rom.bin                        # interactive (real time)
    python3 python/test_z80_ti84plus.py            # the regression test

## Building tizsim

1. z80e needs `scas` (KnightOS/scas, `make install install_lib_static DESTDIR=...`) and `libreadline-dev`; build it
   with `cmake -DSCAS_INCLUDES=... -DSCAS_LIBRARIES=.../libscas.a . && make` (this produces `libz80e.so`).
2. z80e prints the link TX bytes with `printf` in `libz80e/src/runloop/runloop.c`. tizsim needs the hook
   `void (*z80e_link_tx_hook)(int c)` that replaces that printf (a 6-line patch, `link_socket_update`), and the
   debug printfs of `link.c` removed.
3. `gcc -O2 -o tizsim tizsim.c -I<z80e>/libz80e/include -L<z80e>/frontends/libz80e/bin -lz80e`

## Hardware used (TI-84 Plus, as emulated by z80e)

| what | how |
|---|---|
| memory | 0x0000-0x3FFF flash page 0, 0x4000-0x7FFF flash page 1 (port 6), 0x8000-0xBFFF RAM page 1 (port 7), 0xC000-0xFFFF RAM page 0 (port 5) |
| tick | first timer (about 512Hz, port 4 speed + port 3 mask bit 1) |
| console | link assist: port 8 enable, 9 status (bit 4 rx_ready, bit 5 tx_ready), 0x0A rx, 0x0D tx |
| disk | FAT12 image in flash pages 2..61 (read through bank A, `_ti_xfer` in crt0.s); writes go to a RAM overlay (RAM pages 2..5 = 127 sectors, lost at power off; its table is at 0x8C68 in the kernel RAM) |
| LCD | T6A04 in the 6-bit mode (ports 0x10/0x11): a 16 x 8 text screen of 6x8 cells, 5x7 font, scrolling by the Z register; every console byte goes to the link and the LCD (`tizsim -l f` dumps it) |

Not done: the keypad (no input from the calculator's keys), persisting the overlay to flash, running on a real calculator.
