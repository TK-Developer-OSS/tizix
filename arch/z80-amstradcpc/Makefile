# ARCH=z80-amstradcpc: the Amstrad CPC line (Z80).
#   make ARCH=z80-amstradcpc [DEVICE=cpc6128] [all|clean|run|...]
# DEVICE picks the machine (a directory next to this Makefile), default cpc6128:
#   Amstrad CPC 6128, 128KB RAM, 3" floppy (uPD765), runs on MAME (and Caprice32).
DEVICE ?= cpc6128
DIR = $(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
Makefile: ;
check:
	@test -f $(DIR)/Makefile || { echo "z80-amstradcpc: no device '$(DEVICE)'"; exit 1; }
all: check
	$(MAKE) -C $(DIR) ARCH=z80-amstradcpc all
%: check
	$(MAKE) -C $(DIR) ARCH=z80-amstradcpc $@
