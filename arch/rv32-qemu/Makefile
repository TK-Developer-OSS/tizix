# tizix arch/rv32-qemu
#   Toolchain: riscv64-unknown-elf-gcc (-march=rv32im_zicsr -mabi=ilp32)
#   Run: QEMU riscv32 virt (-bios none, M mode only). For verifying ahead of the FPGA version (rv32-fpga-papiliolx6).
#   Usage: make ARCH=rv32-qemu        ... kernel + disk image
#           make ARCH=rv32-qemu run    ... interactive on QEMU (quit with Ctrl+A X)
#
#   The parts that are the same for every arch (the shared kernel, the command list, the disk image) are in ../common-gcc.mk.

CROSS   ?= riscv64-unknown-elf-
CC       = $(CROSS)gcc
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy

OBJ  = ../../build/arch/rv32-qemu/obj

TICK_HZ ?= 100
# -mcmodel=medany: 0x80000000 is outside signed 32 bits, so absolute addresses (lui) cannot reach it. Use PC-relative code.
CPU     = -march=rv32im_zicsr -mabi=ilp32 -mcmodel=medany
CFLAGS  = $(CPU) -ffreestanding -fomit-frame-pointer -Wall -Wextra -Os \
          -DARCH_RV32_QEMU -DTICK_HZ=$(TICK_HZ) $(KINC) -include include/plat.h
# Commands are PC-relative, and only the words that hold an address itself are fixed up by the relocation table (mkcmd.py). gp-relative code is not used.
UCFLAGS = $(CPU) -mno-relax -ffreestanding -fomit-frame-pointer -O2 -Wall -Wextra \
          -DARCH_RV32_QEMU -DTICK_HZ=$(TICK_HZ) -Iinclude -include include/plat.h -DTZSH_SMALL

DISK_IMG = $(OBJ)/disk.img
DISK_KB  = 16384

.PHONY: all run clean
all: $(OBJ)/kernel.elf $(DISK_IMG)

include ../common-gcc.mk

$(OBJ)/ucmd_tzsh.o: UCFLAGS += -Os

LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

AOBJ = $(OBJ)/crt0.o $(OBJ)/kmain.o $(OBJ)/console.o $(OBJ)/diskio.o $(OBJ)/loader.o
KOBJ = $(AOBJ) $(LOBJ) $(SOBJ) $(FSBACKEND)

$(OBJ)/kernel.elf: $(KOBJ) link-kernel.ld
	$(LD) -m elf32lriscv -T link-kernel.ld -o $@ $(KOBJ) $(LIBGCC)
	@sz=$$($(CROSS)size $@ | awk 'NR==2{print $$1+$$2+$$3}'); echo "kernel = $$sz bytes (RAM 0x100000 = 1MB, including a 16KB stack)"; \
	 [ $$sz -le 1032192 ] || { echo "*** kernel exceeds 1MB minus the stack ***"; exit 1; }

# ---- external commands (the list and the compilation are in ../common-gcc.mk) ----
UCRT  = $(OBJ)/crt0cmd.o
ULIB  = $(OBJ)/usyscall.o

$(OBJ)/crt0cmd.o: user/crt0cmd.S | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<
$(OBJ)/usyscall.o: user/usyscall.S | $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

# Link the same .o at two addresses and build the relocation table from the words that differ (tools/mkcmd.py)
CBASE_A = 0x00010000
CBASE_B = 0x00020000
$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld tools/mkcmd.py
	$(LD) -m elf32lriscv --no-relax -T user/cmd.ld --defsym CBASE=$(CBASE_A) -o $(OBJ)/$*.a.elf $(UCRT) $< $(ULIB)
	$(LD) -m elf32lriscv --no-relax -T user/cmd.ld --defsym CBASE=$(CBASE_B) -o $(OBJ)/$*.b.elf $(UCRT) $< $(ULIB)
	$(OBJCOPY) -O binary $(OBJ)/$*.a.elf $(OBJ)/$*.a.raw
	$(OBJCOPY) -O binary $(OBJ)/$*.b.elf $(OBJ)/$*.b.raw
	python3 tools/mkcmd.py $(OBJ)/$*.a.raw $(OBJ)/$*.b.raw $(CBASE_A) $(CBASE_B) $@

QEMU = qemu-system-riscv32 -M virt -bios none -nographic -m 128M \
       -kernel $(OBJ)/kernel.elf -device loader,file=$(DISK_IMG),addr=0x82000000

run: all
	$(QEMU)

clean:
	$(RM) -r $(OBJ)
