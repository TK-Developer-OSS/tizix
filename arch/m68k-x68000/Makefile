# tizix arch/m68k-x68000
#   Toolchain: m68k-elf-gcc 8.3.0 (freestanding, -mcpu=68000), looked up in ~/tools/m68k/bin
#   Target: Sharp X68000 (MC68000) on XM6G or real hardware. ROM IPL -> boot sector
#     (boot_ipl.s) -> TIZIX.SYS ($10000). Console and disk go through the ROM's IOCS (TRAP #15).
#   Disk: X68000 2HD floppy (.xdf, 1232KB, 1024 bytes/sector, FAT12)
#   Usage: make -C arch/m68k-x68000         ... kernel.bin + boot_ipl.bin + disk.xdf
#          make -C arch/m68k-x68000 xm6     ... print how to boot it (XM6G / hardware)
#          make -C arch/m68k-x68000 test    ... rebuild the disk, run the regression on XM6G

M68K    ?= $(HOME)/tools/m68k/bin
CC      = $(M68K)/m68k-elf-gcc
LD      = $(M68K)/m68k-elf-ld
AS      = $(M68K)/m68k-elf-as
OBJCOPY = $(M68K)/m68k-elf-objcopy
NM      = $(M68K)/m68k-elf-nm

OBJ  = ../../build/arch/m68k-x68000/obj

# The ROM sets MFP Timer-C to 10ms; use it as the tick as it is
TICK_HZ ?= 100

# FatFs runs with 1024-byte sectors (X68000 2HD)
CFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -Wall -Wextra \
         -Os -DARCH_M68K_X68000 -DTICK_HZ=$(TICK_HZ) -DFF_MIN_SS=1024 -DFF_MAX_SS=1024 -DFF_JMPBOOT_BRA68K \
         $(KINC) -include include/plat.h

UCFLAGS = -mcpu=68000 -ffreestanding -fomit-frame-pointer -O2 -Wall -Wextra \
          -mpcrel -DARCH_M68K_X68000 -DTICK_HZ=$(TICK_HZ) -Iinclude -include include/plat.h
UCFLAGS += -DTZSH_SMALL

DISK_XDF = $(OBJ)/disk.xdf

.PHONY: all xm6 test clean
all: $(OBJ)/kernel.bin $(DISK_XDF)

include ../common-gcc.mk

$(OBJ)/ucmd_tzsh.o: UCFLAGS += -Os

LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/diskio.o $(OBJ)/loader.o
KOBJ = $(AOBJ) $(LOBJ) $(SOBJ) $(FSBACKEND)

$(OBJ)/kernel.elf: $(KOBJ) link-kernel.ld
	$(LD) -T link-kernel.ld -o $@ $(KOBJ) $(LIBGCC)

# Kernel image + bss must fit in KERNEL_AREA of plat.h (checked through _bss_end).
$(OBJ)/kernel.bin: $(OBJ)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@end=$$($(NM) $< | awk '/ _bss_end$$/ {print $$1}'); \
	 sz=$$(stat -c%s $@); \
	 echo "kernel.bin = $$sz bytes, bss end = 0x$$end (kernel area ends 0x019000)"; \
	 [ $$((0x$$end)) -le $$((0x19000)) ] || { echo "*** kernel + bss exceed the kernel area (36KB) ***"; exit 1; }

UCRT  = $(OBJ)/crt0cmd.o
ULIB  = $(OBJ)/usyscall.o

$(OBJ)/crt0cmd.o: user/crt0cmd.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/usyscall.o: user/usyscall.s | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld
	$(LD) -T user/cmd.ld -o $@ $(UCRT) $< $(ULIB)

# Boot sector code (from offset 0x3E)
$(OBJ)/boot_ipl.o: boot_ipl.s | $(OBJ)
	$(AS) -m68000 -o $@ $<
$(OBJ)/boot_ipl.bin: $(OBJ)/boot_ipl.o
	$(OBJCOPY) -O binary -j .text $< $@

$(DISK_XDF): tools/make_xdf.py etc/rc $(OBJ)/boot_ipl.bin $(OBJ)/kernel.bin $(USERBINS)
	python3 tools/make_xdf.py $@ $(OBJ)/boot_ipl.bin $(OBJ)/kernel.bin $(USERBINS)

xm6: $(DISK_XDF)
	@echo "============================================================"
	@echo " $(DISK_XDF) (2HD, 1.23MB)"
	@echo " XM6G: have IPLROM.DAT and CGROM.DAT ready, put this .xdf in FDD0 and reset."
	@echo " Hardware: write it to a 2HD floppy and boot from FDD0 (at your own risk)."
	@echo "============================================================"

# Regression on XM6G (see tools/xm6run.py). The image is rebuilt first so the run starts
# from a pristine disk.
test:
	rm -f $(DISK_XDF)
	$(MAKE) $(DISK_XDF)
	python3 tools/xm6run.py tests tools/x68k_tests.txt

clean:
	rm -rf $(OBJ)
