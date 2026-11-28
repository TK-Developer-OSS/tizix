# ARCH=z80-rc2014: the RC2014 modular Z80 computer.
#   make ARCH=z80-rc2014 [DEVICE=romwbw] [all|clean|run|...]
# DEVICE picks the configuration (a directory next to this Makefile), default romwbw:
#   an RC2014 booting RomWBW (Zed / Zed Pro: 512K RAM / 512K flash module), SIO/2 serial, Compact Flash; runs on MAME.
DEVICE ?= romwbw
DIR = $(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
Makefile: ;
check:
	@test -f $(DIR)/Makefile || { echo "z80-rc2014: no device '$(DEVICE)'"; exit 1; }
all: check
	$(MAKE) -C $(DIR) ARCH=z80-rc2014 all
%: check
	$(MAKE) -C $(DIR) ARCH=z80-rc2014 $@
