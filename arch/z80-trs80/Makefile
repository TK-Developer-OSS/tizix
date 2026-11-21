# ARCH=z80-trs80: the TRS-80 line (Z80 only).
#   make ARCH=z80-trs80 [DEVICE=model4p] [all|clean|run|...]
# DEVICE picks the machine (a directory next to this Makefile), default model4p:
#   TRS-80 Model 4P, 64KB RAM, floppy (the boot ROM is the free one built into xtrs).
DEVICE ?= model4p
DIR = $(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
Makefile: ;
check:
	@test -f $(DIR)/Makefile || { echo "z80-trs80: no device '$(DEVICE)'"; exit 1; }
all: check
	$(MAKE) -C $(DIR) ARCH=z80-trs80 all
%: check
	$(MAKE) -C $(DIR) ARCH=z80-trs80 $@
