# tizix arch/rpi-pico
#   ツールチェイン: arm-none-eabi-gcc / CMake (Pico SDK) + gcc 系共通 common-gcc.mk
#   ターゲット: Raspberry Pi Pico / Pico W (RP2040)

ARCH = rpi-pico
ROOTDIR = $(abspath ../..)
SRCDIR = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJ = build/obj

CC      = arm-none-eabi-gcc
LD      = arm-none-eabi-ld
OBJCOPY = arm-none-eabi-objcopy
HOSTCC  = gcc

# Pico W 用フラット32 CFLAGS (インクルードパスに user/ と src/libc を明示追加)
TICK_HZ ?= 100
CFLAGS = -mcpu=cortex-m0plus -mthumb -ffreestanding -fomit-frame-pointer -Wall -Wextra \
         -Os -DARCH_RPI_PICO -DTICK_HZ=$(TICK_HZ) $(KINC) \
         -include include/plat.h
UCFLAGS = -mcpu=cortex-m0plus -mthumb -ffreestanding -fomit-frame-pointer -O2 -Wall -Wextra \
          -DARCH_RPI_PICO -DTIZIX_USERCMD -DTICK_HZ=$(TICK_HZ) \
          -Iinclude -I$(USERDIR) -I$(SRCDIR)/libc \
          -include include/plat.h

DISK_IMG = sdcard.img
DISK_KB  = 32768

.PHONY: all kernel disk install clean FORCE

all: $(DISK_IMG) build/kernel.uf2
	cp build/kernel.uf2 ../../kernel.uf2
	@echo "=== Pico W kernel.uf2 and sdcard.img built successfully ==="

build/kernel.uf2: FORCE   # 依存を持たない規則なので、古い uf2 を最新とみなさない(cmake 側が差分を見る)
	mkdir -p build
	@if [ ! -f build/CMakeCache.txt ]; then cd build && cmake ..; fi
	cd build && make

FORCE:

kernel: all

# ---- 共通 gcc 系ルール (common-gcc.mk) のインクルード ----
include ../common-gcc.mk

# 外部コマンド用 crt0cmd と usyscall (arch/rpi-pico/user/ 内の ARM 向けを指す)
UCRT   = $(OBJ)/crt0cmd.o
ULIB   = $(OBJ)/usyscall.o
LIBGCC = $(shell $(CC) $(UCFLAGS) -print-libgcc-file-name)

$(OBJ)/crt0cmd.o: user/crt0cmd.s | $(OBJ)
	@mkdir -p $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(OBJ)/usyscall.o: user/usyscall.s | $(OBJ)
	@mkdir -p $(OBJ)
	$(CC) $(UCFLAGS) -c -o $@ $<

# tzsh だけ -Os でサイズを抑える
$(OBJ)/ucmd_tzsh.o: UCFLAGS += -Os

$(OBJ)/%.bin: $(OBJ)/ucmd_%.o $(UCRT) $(ULIB) user/cmd.ld
	@mkdir -p $(OBJ)
	$(LD) -T user/cmd.ld -o $(OBJ)/$*.elf $(UCRT) $< $(ULIB) $(LIBGCC)
	$(OBJCOPY) -O binary $(OBJ)/$*.elf $@




disk: $(DISK_IMG)

install: all
	cp build/kernel.uf2 ../../kernel.uf2
	@echo "=== Pico W kernel.uf2 installed successfully ==="

clean:
	rm -rf build $(DISK_IMG) ../../kernel.uf2
