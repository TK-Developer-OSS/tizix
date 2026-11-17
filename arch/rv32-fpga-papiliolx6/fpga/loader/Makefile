# Boot loader (IMEM ROM). make -C fpga/loader [CLK_HZ=...]  -> loader.bin, imem_image.vhd
CROSS ?= riscv64-unknown-elf-
CLK_HZ ?= 32000000
UART_BAUD ?= 115200
all: imem_image.vhd
loader.elf: crt0.S loader.c link.ld
	$(CROSS)gcc -march=rv32im_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -Os -ffreestanding -nostdlib -fno-pic \
	  -DCLK_HZ=$(CLK_HZ)UL -DUART_BAUD=$(UART_BAUD)UL -T link.ld -o $@ crt0.S loader.c
loader.bin: loader.elf
	$(CROSS)objcopy -O binary $< $@
	@echo "loader = `wc -c < $@` bytes (ROM 4096)"
imem_image.vhd: loader.bin mkimem.py
	python3 mkimem.py loader.bin $@
clean:
	$(RM) loader.elf loader.bin imem_image.vhd
