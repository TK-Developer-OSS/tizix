# ARCH=avr-uno: tizix-mcu (Arduino Uno / Nano).  The sources live in mcu/avr-uno;
# this only forwards every goal there (all, size, flash, test, clean).
.DEFAULT_GOAL := all
.PHONY: all
all:
	$(MAKE) -C ../../mcu/avr-uno all
%:
	$(MAKE) -C ../../mcu/avr-uno $@
