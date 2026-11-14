# ARCH=msx: the MSX line (Z80 only; MSX2/2+ first, MSX1 later, turboR's R800 is not a target).
#   make ARCH=msx [DEVICE=msx2-64kb-2dd] [all|clean|...]
# DEVICE picks the machine class (a directory next to this Makefile), default msx2-64kb-2dd:
#   MSX2, 64KB RAM, 3.5" 2DD floppy (720KB, FAT12, 512B sectors, 9 sectors/track), with the disk ROM built into the machine.
# A machine that gets its disk ROM from an external FDD cartridge is a separate DEVICE (not supported yet).
DEVICE ?= msx2-64kb-2dd
DIR = $(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
Makefile: ;
check:
	@test -f $(DIR)/Makefile || { echo "msx: no device '$(DEVICE)' (have: $$(ls -d msx*/))"; exit 1; }
all: check
	$(MAKE) -C $(DIR) ARCH=msx all
%: check
	$(MAKE) -C $(DIR) ARCH=msx $@
