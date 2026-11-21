#===================================================================
# tizix - arch/z80-ti84plus (Texas Instruments TI-84 Plus, developed on the z80e emulator)
#   ROM layout: flash page 0-1 (0x0000-0x7FFF) = the kernel (XIP), page 2.. = a FAT12 image (read-only for now).
#   RAM 0x8000-0xFFFF is the same layout as arch/z80board. Console = the link port through its link assist.
#   Run it with tools/tizsim (a small frontend of libz80e, see tools/README).
#===================================================================
ARCH    = z80-ti84plus
ROOTDIR = $(abspath ../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80-ti84plus/obj

# share the z80board branches of src/ (same memory map and ring buffer); the timer is 512Hz
PLAT_DEF    = -DARCH_Z80BOARD -DARCH_TI84PLUS -DTICK_HZ=512
PLAT_DEF_AS =

KOBJ_ARCH = console.rel

CODE_LOC = 0x0080
DATA_LOC = 0x8000

DISK   = rom.bin
FATDSK = flashdisk.img

include ../common-sdcc.mk

# kernel image (32KB) followed by the flash disk (pages 2..61)
$(DISK): $(OBJDIR)/kernel.ihx $(FATDSK)
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@python3 tools/romsize.py $(OBJDIR)/kernel.map
	cp $(OBJDIR)/kernel.img $@
	cat $(FATDSK) >> $@
