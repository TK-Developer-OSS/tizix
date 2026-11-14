# The emulator-only stand-in disk ROM (C-BIOS cannot boot from a disk).
OBJ ?= $(CURDIR)/../../../build/arch/msx/emu
all: $(OBJ)/diskrom.rom
$(OBJ):
	mkdir -p $@
$(OBJ)/loader.rel: loader.s ../dskio.inc | $(OBJ)
	sdasz80 -o $@ loader.s
$(OBJ)/loader.ihx: $(OBJ)/loader.rel
	sdldz80 -n -i $@ $<
$(OBJ)/loader.inc: $(OBJ)/loader.ihx ../msx2-64kb-2dd/ihx2inc.py
	python3 ../msx2-64kb-2dd/ihx2inc.py $< loader_image 0x9000 0x300 > $@
$(OBJ)/diskrom.rel: diskrom.s $(OBJ)/loader.inc
	sdasz80 -I$(OBJ) -o $@ diskrom.s
$(OBJ)/diskrom.ihx: $(OBJ)/diskrom.rel
	sdldz80 -n -i $@ $<
$(OBJ)/diskrom.rom: $(OBJ)/diskrom.ihx
	makebin -s 32768 $< $(OBJ)/diskrom.img
	dd if=$(OBJ)/diskrom.img of=$@ bs=1 skip=16384 count=16384 2>/dev/null

# the openMSX machine file, with the path of the disk ROM filled in
all: $(OBJ)/tizix_msx2.xml
$(OBJ)/tizix_msx2.xml: tizix_msx2.xml.in $(OBJ)/diskrom.rom
	sed 's|@DISKROM@|$(OBJ)/diskrom.rom|' tizix_msx2.xml.in > $@

# openMSX finds a machine by name: put the file where it looks (the user's own directory)
install: $(OBJ)/tizix_msx2.xml
	mkdir -p $(HOME)/.openMSX/share/machines
	cp $(OBJ)/tizix_msx2.xml $(HOME)/.openMSX/share/machines/tizix_msx2.xml
.PHONY: install
