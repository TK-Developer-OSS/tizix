# tizix-mcu for Arduino Uno / Nano (ATmega328P). Independent of the top-level Makefile.
#   make          build build/mcu.hex and show the size
#   make flash    write it through the Optiboot bootloader (PORT=/dev/ttyACM0)
#   make test     run the serial smoke test against the board
#   make run      flash, then open the console (picocom; leave with Ctrl-A Ctrl-X)
MCU    = atmega328p
F_CPU  = 16000000UL
PORT  ?= /dev/ttyACM0
BAUD   = 115200
FLASH_MAX = 32256            # 32KB minus the 512B Optiboot area

CC      = avr-gcc
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -std=gnu99 -Os -Wall -Wextra \
          -ffunction-sections -fdata-sections -fshort-enums -I../common -I$(TZSH)
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

TZSH = ../../user/tzsh
# the tzsh scripts baked into flash, run as `tzsh NAME` (NAME = the file name, e.g. tzsh blink.sh)
SCRIPT_FILES = $(TZSH)/conformance/mcu.sh ../scripts/blink.sh

SRC = ../common/main.c ../common/io.c ../common/shell.c ../common/cmds.c \
      ../common/dev.c hal.c $(TZSH)/tzsh_core.c build/scripts.c
OBJ = $(patsubst %.c,build/%.o,$(notdir $(SRC)))
VPATH = ../common:$(TZSH):build

all: build/mcu.hex size

build/%.o: %.c ../common/mcu.h ../common/tzsh_host.h $(TZSH)/tzsh_core.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/scripts.c: $(SCRIPT_FILES) ../tools/script2c.py | build
	python3 ../tools/script2c.py $(SCRIPT_FILES) > $@

build/mcu.elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

build/mcu.hex: build/mcu.elf
	avr-objcopy -O ihex -R .eeprom $< $@

build:
	mkdir -p build

size: build/mcu.elf
	@avr-size -C --mcu=$(MCU) build/mcu.elf
	@t=$$(avr-size -A build/mcu.elf | awk '/^\.(text|data)/ {s+=$$2} END {print s}'); \
	 echo "flash used: $$t / $(FLASH_MAX)"; \
	 [ $$t -le $(FLASH_MAX) ] || { echo "ERROR: overlaps the bootloader"; exit 1; }

flash: build/mcu.hex
	avrdude -c arduino -p m328p -P $(PORT) -b $(BAUD) -U flash:w:build/mcu.hex:i

run: flash
	picocom -b $(BAUD) $(PORT)

test:
	python3 tests/smoke.py $(PORT)

clean:
	rm -rf build

.PHONY: all size flash run test clean
