#===================================================================
# tizix - arch/z80-rc2014 (RC2014 with RomWBW: Z80 7.3728MHz, 512K RAM / 512K flash module, SIO/2, Compact Flash)
#   make ARCH=z80-rc2014      builds disks/cf.hd (a raw Compact Flash image, 4MB)
#   Boot: RomWBW's loader (boot from the CF disk, "2" at the Boot Selection prompt on MAME) reads the card's sector 2 (the
#   boot info) and loads sector 3 (boot.s) to 0xA000.  boot.s reads the kernel (64 sectors from sector 4) to 0x0100
#   with the CF card's own registers and jumps to 0x0100.  The card is one FAT12 volume from sector 0 whose reserved
#   sectors hold the boot info, boot.s and the kernel.
#   RAM is the 64KB RomWBW leaves mapped (its user bank below 0x8000, its common bank above): the kernel takes all of it.
#   Console: SIO/2 channel A (as RomWBW set it, 115200 8N1).  Timer: SIO/2 channel B sends to nothing; its "transmitter
#   empty" interrupt is the tick (clocked by the Dual Clock module's CLOCK2: 0.3072MHz, see crt0.s).
#===================================================================
ARCH    = z80-rc2014
ROOTDIR = $(abspath ../../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80-rc2014/obj

# 0.3072MHz / 64 = 4800 bit/s, 11 bits a byte (8 data, 2 stop) = 436.4 interrupts/s, crt0.s counts 7 of them: 62.3Hz
PLAT_DEF    = -DARCH_Z80_RC2014 -DTICK_HZ=62
PLAT_DEF_AS =
KOBJ_ARCH   = console.rel

CODE_LOC = 0x0120
DATA_LOC = 0x8000
NSEC     = 254          # kernel image = 254 * 128B = 63.5 sectors of 512B, from 0x0100 (boot.s KSECT = 64)

DISK   = disks/cf.hd
FATDSK = disks/fat.img

include ../../common-sdcc.mk

$(OBJDIR)/boot.rel: boot.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ boot.s
$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel
	sdldz80 -n -i $@ $<

$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@python3 tools/kernel_end.py $(OBJDIR)/kernel.map $(NSEC)
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# disks/fat.img = the FAT12 volume (made by mkfatdisk.sh, kept between builds; never modified here);
# disks/cf.hd = the volume with the boot info, boot.s and the kernel in its reserved sectors (rebuilt when they change)
$(DISK): $(OBJDIR)/boot.ihx $(OBJDIR)/kernel.bin $(FATDSK) mkdisk.py
	python3 mkdisk.py $(OBJDIR)/boot.ihx $(OBJDIR)/kernel.bin $(FATDSK) $@

# run on MAME (tools/run.sh)
run:
	@test -f $(DISK) || $(MAKE) all
	sh tools/run.sh $(DISK)
.PHONY: run
