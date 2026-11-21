#===================================================================
# tizix - arch/z80-trs80 (TRS-80 Model 4P: Z80 4MHz, 64KB RAM, floppy, memory-mapped 80x24 text + keyboard)
#   make ARCH=z80-trs80      builds disks/disk4p-0 (a JV3 image for xtrs; also usable as the floppy contents)
#   Boot: the 4P boot ROM reads track 0, sector id 1 (256B) to 0x4300 and jumps there.  boot.s moves itself to 0xF000
#   and reads the kernel (127 sectors) to 0x0100.  The disk is a 512B header (id 0 unused, id 1 = boot) followed by one
#   FAT12 volume of 720KB whose reserved sectors hold the kernel image.  FatFs sector n = two 256B sectors, 2+2n.
#   RAM is 0x0000-0xF3FF (map 2); 0xF000 and up is not used by the kernel (block 7).  Same layout as arch/msx.
#===================================================================
ARCH    = z80-trs80
ROOTDIR = $(abspath ../../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80-trs80/obj

# the 4P timer is 30Hz.  ARCH_Z80_TRS80: the trims in src/ that the 0x8000 limit needs (as ARCH_Z80_MSX)
PLAT_DEF    = -DARCH_Z80_TRS80 -DTICK_HZ=30
PLAT_DEF_AS =
KOBJ_ARCH   = console.rel

CODE_LOC = 0x0120
DATA_LOC = 0x8000
NSEC     = 254          # kernel image = 254 * 128B = 127 sectors of 256B, from 0x0100 (boot.s KSECT)

DISK   = disks/disk4p-0
FATDSK = disks/fat.img

include ../../common-sdcc.mk

$(OBJDIR)/boot.rel: boot.s | $(OBJDIR)
	$(AS) $(ASFLAGS) -o $@ boot.s
$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel
	sdldz80 -n -i $@ $<

$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@python3 kernel_end.py $(OBJDIR)/kernel.map $(NSEC)
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# disks/fat.img = the FAT12 volume (made by mkfatdisk.sh, kept between builds; never modified here);
# disks/disk4p-0 = boot header + volume + the kernel in its reserved sectors, as JV3 (rebuilt when the kernel or boot changes)
$(DISK): $(OBJDIR)/boot.ihx $(OBJDIR)/kernel.bin $(FATDSK) mkdisk.py
	python3 mkdisk.py $(OBJDIR)/boot.ihx $(OBJDIR)/kernel.bin $(FATDSK) $@

# run on xtrs (tools/run.sh sets up a display and the serial pty)
# (no dependency on $(DISK): that would rebuild every tzcc command each time; `make all` first, or it is built when missing)
run:
	@test -f $(DISK) || $(MAKE) all
	sh tools/run.sh $(DISK)
.PHONY: run
