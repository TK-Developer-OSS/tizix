# WiFi 実験(#109 W1)。windowed ABI、newlib / libgcc を使う。
CROSS = xtensa-esp32-elf-
CC    = $(CROSS)gcc
ROMLD = $(HOME)/tools/esp32-wifi-blobs/ld
BLOBS = $(HOME)/tools/esp32-wifi-blobs
# ESP-IDF のヘッダ(Arduino の写し)。include/<部品>/include と port/esp32/include 等を全部 -I に
INC = -I. -I$(BLOBS)/config -I$(BLOBS)/include/freertos/config/include/freertos -I$(BLOBS)/include/freertos/config/xtensa/include/freertos -I$(BLOBS)/include/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos -I$(BLOBS)/include/freertos/FreeRTOS-Kernel/include/freertos $(shell find $(BLOBS)/include -type d \( -name include -o -name platform_include -o -path '*esp32/include' -o -path '*esp32/register' \) | grep -v -E 'esp32(c|s|h|p)|riscv|linux|/test' | sed 's/^/-I/')
CFLAGS = -Os -g -Wall -mlongcalls -mtext-section-literals -ffunction-sections -fdata-sections -fno-common -DESP_PLATFORM
LDFLAGS = -nostartfiles -Wl,--gc-sections -T link.ld -L$(ROMLD) -T esp32.rom.ld -T esp32.rom.api.ld -T esp32.rom.libgcc.ld -T esp32.peripherals.ld
LIBS = $(addprefix $(BLOBS)/lib/, $(BLIBS))

OBJS = start.o main.o $(EXTRA_OBJS)

all: wt.img drom.bin irom.bin

wt.elf: $(OBJS) link.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -Wl,--start-group $(LIBS) -lc -lgcc -Wl,--end-group -Wl,-Map,wt.map
	$(CROSS)size -A $@ | grep -E '^\.(iram|dram|bss|flash)'

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<
%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

ram.elf: wt.elf
	$(CROSS)objcopy -R .flash.text -R .flash.rodata $< $@
wt.img: ram.elf
	esptool --chip esp32 elf2image --flash-mode dio --flash-freq 40m --flash-size 4MB -o $@ $< > /dev/null
drom.bin: wt.elf
	$(CROSS)objcopy -O binary -j .flash.rodata $< $@
irom.bin: wt.elf
	$(CROSS)objcopy -O binary -j .flash.text $< $@

clean:
	rm -f *.o *.elf *.img *.bin wt.map
