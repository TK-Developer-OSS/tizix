#===================================================================
# tizix - arch/z80-amstradcpc (Amstrad CPC 6128: Z80 4MHz, 128KB RAM, uPD765 floppy)
#   make ARCH=z80-amstradcpc     builds disks/cpc.dsk (a "MV - CPC" DSK image of a "system format" disk: 40 tracks,
#                                9 x 512B sectors, ids 0x41-0x49)
#   Boot: |CPM at the BASIC prompt makes AMSDOS read track 0 sector 0x41 to 0x0100 and jump there.  boot.s moves itself
#   to 0x9000, switches to RAM configuration 1 (banks 0,1,2,7) with both ROMs off, reads the kernel (64 sectors) to
#   0x0100 and hi.s (8 sectors) to 0xF000, and jumps to 0x0100.  The disk is the boot sector followed by one FAT12
#   volume (359 sectors) whose reserved sectors hold the kernel and hi.s.
#   RAM is 0x0000-0xEFFF for the kernel and processes; 0xF000 and up (block 7) is hi.s: the screen and disk code (the
#   screen is bank 3, only visible in configuration 3), the font, the key tables and a text copy of the screen.  Same
#   layout as arch/z80-zxspectrum otherwise.
#===================================================================
ARCH    = z80-amstradcpc
ROOTDIR = $(abspath ../../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80-amstradcpc/obj

# the interrupt is 300Hz, crt0.s passes on one in six.  ARCH_Z80_AMSTRADCPC: the trims in src/ that the 0x8000 limit
# needs (as ARCH_Z80_MSX)
PLAT_DEF    = -DARCH_Z80_AMSTRADCPC -DTICK_HZ=50
PLAT_DEF_AS =
KOBJ_ARCH   = console.rel

CODE_LOC = 0x0120
DATA_LOC = 0x8000
NSEC     = 254          # kernel image = 254 * 128B = 63.5 sectors of 512B, from 0x0100 (boot.s KSECT = 64)

DISK   = disks/cpc.dsk
FATDSK = disks/fat.img

include ../../common-sdcc.mk

$(OBJDIR)/boot.rel: boot.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ boot.s
$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel
	sdldz80 -n -i $@ $<
$(OBJDIR)/hi.rel: hi.s font.inc keys.inc | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ hi.s
$(OBJDIR)/hi.ihx: $(OBJDIR)/hi.rel
	sdldz80 -n -i $@ $<

$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@python3 tools/kernel_end.py $(OBJDIR)/kernel.map $(NSEC)
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# disks/fat.img = the FAT12 volume (made by mkfatdisk.sh, kept between builds; never modified here);
# disks/cpc.dsk = boot sector + volume + the kernel and hi.s in its reserved sectors, as a DSK (rebuilt when they change)
$(DISK): $(OBJDIR)/boot.ihx $(OBJDIR)/hi.ihx $(OBJDIR)/kernel.bin $(FATDSK) mkdisk.py
	python3 mkdisk.py $(OBJDIR)/boot.ihx $(OBJDIR)/hi.ihx $(OBJDIR)/kernel.bin $(FATDSK) $@

# run on MAME in a window (tools/run.sh)
# (no dependency on $(DISK): that would rebuild every tzcc command each time; `make all` first, or it is built when missing)
run:
	@test -f $(DISK) || $(MAKE) all
	sh tools/run.sh $(DISK)
.PHONY: run
