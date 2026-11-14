#===================================================================
# tizix - arch/msx/msx2-64kb-2dd : MSX2, 64KB RAM, 3.5" 2DD, disk ROM built in
#   Normally run from the top:  make ARCH=msx   (DEVICE defaults to this)
#   The disk is one 720KB FAT12 volume; its reserved sectors hold the boot sector (sector 0), s2 (sector 1) and the
#   kernel image (sectors 2-65).  Not run on a machine or an emulator yet.
#===================================================================
ARCH    = msx
ROOTDIR = $(abspath ../../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/msx/obj

# TICK_HZ: the VDP vblank is 60Hz on NTSC machines (a 50Hz PAL machine runs the clock 17% slow).
# ARCH_Z80_MSX: trims in src/ that the 0x8000 limit needs, as ARCH_Z80_PC8801 does.
PLAT_DEF = -DARCH_Z80_MSX -DTICK_HZ=60
PLAT_DEF_AS = -I$(OBJDIR)
KOBJ_ARCH   = console.rel

CODE_LOC = 0x0120
DATA_LOC = 0x8000
NSEC     = 254          # kernel image sectors of 128B; s2.s (NBYTES) and the 66 reserved sectors (mkfatdisk.sh) follow from it

DISK   = disks/tizix.dsk
FATDSK = disks/tizix.dsk

include ../../common-sdcc.mk

$(OBJDIR)/s2.rel: $(OBJDIR)/tramp.inc

# ---- boot loader pieces: s2 (page 2, 0x8000) goes to disk sector 1; tramp (page 2, 0x8C78) becomes .db lines ----
$(OBJDIR)/s2.rel: s2.s ../dskio.inc | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ s2.s
$(OBJDIR)/s2.ihx: $(OBJDIR)/s2.rel
	sdldz80 -n -i $@ $<

$(OBJDIR)/tramp.rel: tramp.s ../dskio.inc | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ tramp.s
$(OBJDIR)/tramp.ihx: $(OBJDIR)/tramp.rel
	sdldz80 -n -i $@ $<
$(OBJDIR)/tramp.inc: $(OBJDIR)/tramp.ihx ihx2inc.py
	python3 ihx2inc.py $< tramp_image 0x8C78 224 > $@

$(OBJDIR)/boot.rel: boot.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ boot.s
$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel
	sdldz80 -n -i $@ $<

# ---- kernel image (raw bytes from 0x0100) ----
$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@python3 kernel_end.py $(OBJDIR)/kernel.map $(NSEC)
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# the disk needs the boot sector and the kernel image first
$(FATDSK): $(OBJDIR)/boot.ihx $(OBJDIR)/s2.ihx $(OBJDIR)/kernel.bin

# openMSX with C-BIOS and the stand-in disk ROM (../emu); the machine is MSX2, 64KB, one 2DD drive
EMUDIR = ../emu
run: $(FATDSK)
	$(MAKE) -C $(EMUDIR) install
	openmsx -machine tizix_msx2 -script $(EMUDIR)/run.tcl -diska $(FATDSK)

.PHONY: run
