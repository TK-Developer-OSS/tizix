# ARCH=z80-zxspectrum: the ZX Spectrum line (Z80 only).
#   make ARCH=z80-zxspectrum [DEVICE=plus3] [all|clean|run|...]
# DEVICE picks the machine (a directory next to this Makefile), default plus3:
#   ZX Spectrum +3, 128KB RAM, 3" floppy (uPD765), runs on ZEsarUX.
DEVICE ?= plus3
DIR = $(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
Makefile: ;
check:
	@test -f $(DIR)/Makefile || { echo "z80-zxspectrum: no device '$(DEVICE)'"; exit 1; }
all: check
	$(MAKE) -C $(DIR) ARCH=z80-zxspectrum all
%: check
	$(MAKE) -C $(DIR) ARCH=z80-zxspectrum $@
