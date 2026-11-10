# ARCH=mcu-low: the tiny PIC line.  DEVICE picks the chip (a directory in mcu-low/), default pic12f683.
#   make ARCH=mcu-low [DEVICE=pic12f683] [all|size|test|run|clean]  (run: PORT=/dev/ttyUSB0, or none for a pretend device)
DEVICE ?= pic12f683
DIR = ../../mcu-low/$(DEVICE)
.DEFAULT_GOAL := all
.PHONY: all check
check:
	@test -f $(DIR)/Makefile || { echo "mcu-low: no device '$(DEVICE)' (have: $$(ls -d ../../mcu-low/*/ | xargs -n1 basename | grep -v '^vm$$' | tr '\n' ' '))"; exit 1; }
all: check
	$(MAKE) -C $(DIR) all
%: check
	$(MAKE) -C $(DIR) $@
