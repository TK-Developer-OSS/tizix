#===================================================================
# tizix - arch/z80-pc8801
#===================================================================
ARCH    = z80-pc8801
ROOTDIR = $(abspath ../..)
SRCDIR  = $(ROOTDIR)/src
USERDIR = $(ROOTDIR)/user
OBJDIR  = $(ROOTDIR)/build/arch/z80-pc8801/obj

# cpmsim 流用の間は src/ の ARCH_Z80PACK 分岐(dev.c の生ブロック、kernel.c のタイマ)が要る。
# PC-8801 固有のハード層を書く段で外す。
PLAT_DEF = -DARCH_Z80_PC8801 -DARCH_Z80PACK -DTICK_HZ=60   # VRTC(24kHz モニタ)の周波数
PLAT_DEF_AS =
KOBJ_ARCH = console.rel

# Temporary settings
CODE_LOC = 0x0120
DATA_LOC = 0x8000
# boot.s �� NSEC �ƈ�v�K�{(����� DATA_LOC=0x8000)
NSEC     = 254

# Output files
DISK   = disks/drivea.d88
FATDSK = disks/driveb.d88

include ../common-sdcc.mk

# ---- boot binary ----
# sdldz80 creates .ihx from .rel
$(OBJDIR)/boot.ihx: $(OBJDIR)/boot.rel
	sdldz80 -n -i $@ $<

# makebin creates .bin from .ihx
$(OBJDIR)/boot.bin: $(OBJDIR)/boot.ihx
	makebin -s 128 $< $@

# ---- kernel image ----
$(OBJDIR)/kernel.bin: $(OBJDIR)/kernel.ihx
	makebin -s 32768 $< $(OBJDIR)/kernel.img
	@end=`awk '/^_(CODE|HOME|GSINIT|GSFINAL|INITIALIZER) +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ *=/ { a=strtonum("0x" $$2); s=strtonum("0x" $$3); if (a+s > m) m = a+s } END { printf "%d", m+0 }' $(OBJDIR)/kernel.map`; \
	lim=`expr 256 + $(NSEC) \* 128`; \
	if [ "$$end" -gt "$$lim" ]; then \
	  awk -v e=$$end -v l=$$lim 'BEGIN{printf "*** kernel image end 0x%04X > load limit 0x%04X (NSEC=%d). NSEC を増やすかコードを削れ ***\n", e, l, (l-256)/128}' >&2; \
	  exit 1; \
	fi; \
	awk -v e=$$end -v l=$$lim 'BEGIN{printf "kernel image end 0x%04X / load limit 0x%04X  (free %d B)\n", e, l, l-e}'
	dd if=$(OBJDIR)/kernel.img of=$@ bs=128 skip=2 count=$(NSEC) 2>/dev/null

# ---- drive A (boot + kernel) ----
# ---- PC-8801 IPL (256B, org 0xC000) ----
$(OBJDIR)/ipl.bin: $(OBJDIR)/ipl.rel
	sdldz80 -n -i $(OBJDIR)/ipl.ihx $<
	makebin -s 49408 $(OBJDIR)/ipl.ihx $(OBJDIR)/ipl.img
	dd if=$(OBJDIR)/ipl.img of=$@ bs=256 skip=192 count=1 2>/dev/null

$(DISK): $(OBJDIR)/boot.bin $(OBJDIR)/kernel.bin $(OBJDIR)/ipl.bin
	mkdir -p disks
	rm -f $(DISK)
	dd if=/dev/zero of=$(DISK) bs=128 count=2002 2>/dev/null
	dd if=$(OBJDIR)/boot.bin   of=$(DISK) bs=128 seek=0 conv=notrunc 2>/dev/null
	dd if=$(OBJDIR)/kernel.bin of=$(DISK) bs=128 seek=1 conv=notrunc 2>/dev/null
	cp $(DISK) disks/drivea.dsk
	cat $(OBJDIR)/ipl.bin $(OBJDIR)/kernel.bin > $(OBJDIR)/drivea.raw
	python3 make_d88.py $(OBJDIR)/drivea.raw $(DISK) "TIZIX DRIVE A"


