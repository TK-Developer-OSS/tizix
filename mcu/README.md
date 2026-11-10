# tizix-mcu

A separate, tiny line of tizix for microcontrollers (8-bit first). It does **not** share
`src/` with the main kernel: no VFS, no file system, no external commands. Every command is a
builtin, and a command name is just a key in a table (not a path). What it shares with tizix
is the way you use it: the same prompt, and the same command grammar (`gpio` follows
[src/gpiodev.h](../src/gpiodev.h)), so the same skills work on a PIC, an AVR or an ESP32.

```
mcu/
  common/    portable C: shell, builtin commands, gpio grammar, tzsh host glue (chip hooks in mcu.h)
  avr-uno/   Arduino Uno / Nano (ATmega328P): HAL, Makefile, serial smoke test
  tools/     script2c.py: turns a tzsh script into a C string kept in flash
```

Devices hang under `/dev`, as on UNIX: `echo 1 > /dev/gpio/13` writes, `cat /dev/gpio/13` reads,
`ls /dev` lists them. There is no file system; `/dev` is a small fixed table (`common/dev.c`).

The script language is `tzsh` ([user/tzsh/tzsh_core.c](../user/tzsh/tzsh_core.c)): C-like control
around shell-like commands, with `/dev` paths as values.

```
setup {                                         // runs once
  echo 1 > /dev/timer/0/repeat;
  echo 500 > /dev/timer/0/limit;                // the timer interrupt raises /dev/timer/0/finish
}
intr {                                          // runs between statements after an interrupt
  echo 0 > /dev/timer/0/finish;
  if (/dev/gpio/13 == "0") { echo 1 > /dev/gpio/13; }
  else { echo 0 > /dev/gpio/13; }
}
loop {                                          // repeats forever (Ctrl-C stops); write it last
  n = n + 1;
}
```

Values are strings; they become 16 bit numbers only in `+ -` and `< <= > >=`, and an out-of-range
number stops the script (status 255). [user/tzsh/conformance/mcu.sh](../user/tzsh/conformance/mcu.sh)
is the self-test; the scripts in [scripts/](scripts) are examples. Scripts are baked into flash and
run with `tzsh NAME` (for example `tzsh blink.sh`; `cat /blink.sh` shows one).

## Arduino Uno / Nano

```
cd mcu/avr-uno
make          # build, shows flash/RAM use
make flash    # through the Optiboot bootloader (PORT=/dev/ttyACM0)
make test     # serial smoke test against the board
```

Console: 115200 8N1 on the USB serial port. Needs `avr-gcc`, `avr-libc`, `avrdude`.

Commands so far: `help ls cat echo sleep free tzsh`.

**I/O map (what exists, what does not).** Every target gets a table like this one.

| Node | Pins | Notes |
|---|---|---|
| `/dev/gpio/2..7` | D2-D7 (PORTD) | `0`, `1`, `in`, `up`; `cat` reads the level |
| `/dev/gpio/8..13` | D8-D13 (PORTB) | D13 is the on-board L LED |
| `/dev/gpio/14..19` | A0-A5 (PORTC) | used as digital pins |
| `/dev/timer/0/{limit,finish,repeat}` | Timer0 | limit in ms; one shot unless repeat is 1; the 1 ms tick is Timer0 too |
| (none) | D0, D1 | the USB serial console; not available as gpio |

Not available yet: pwm (D5 and D6 are on Timer0, which is the tick, so they will not do pwm), adc, i2c, spi, uart as a node, eeprom. A node that a target does not have is an error (`no such device`), never a silent no-op.

Not yet: typing tzsh statements at the prompt, `VAR=/dev/...` outside scripts, tasks / preemption, other devices (pwm, adc, i2c, uart, eeprom).
