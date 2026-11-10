# PIC12F683 (mcu-low).  Normally run from the top:  make ARCH=mcu-low DEVICE=pic12f683 [all|size|test|clean]
#   make        assemble the virtual machine (vm.asm, with ../vm/examples/blink.s in its EEPROM), the link
#               (node.asm) and the first blink (blink.asm)
#   make size   program words used
#   make test   Python checks of the virtual machine and the converter, then the gpsim runs (link_test.py last)
#   make linktest  only the link + machine test (link_test.py)
#   make run    try the host command (tzlow.py) by hand: PORT=/dev/ttyUSB0 for a device on the link,
#               no PORT for a pretend device on this PC
#   make flash  not yet (needs a PICkit2 tool; none is installed)
GPASM = gpasm -p p12f683
BUILT = vm.hex vm.cod vm.lst node.hex node.cod node.lst blink.hex blink.cod blink.lst prog.inc vm_run.stc node_run.stc node.log

all: vm.hex node.hex blink.hex size

prog.inc: ../vm/examples/blink.s ../vm/nodevm.py
	python3 -c "import sys; sys.path.insert(0, '../vm'); from nodevm import assemble; p = assemble(open('../vm/examples/blink.s').read()); open('prog.inc', 'w').write(''.join('        de %s\n' % ','.join('0x%02X' % b for b in p[i:i+8]) for i in range(0, len(p), 8)))"

vm.hex: vm.asm prog.inc
	$(GPASM) -o $@ vm.asm > /dev/null

%.hex: %.asm
	$(GPASM) -o $@ $< > /dev/null

size:
	@for f in vm node blink; do python3 -c "import sys; t = open('$$f.hex').read().split(); print('%-6s %4d words of 2048' % ('$$f', sum(int(l[1:3], 16) for l in t if l[7:9] == '00' and int(l[3:7], 16) < 0x2000) // 2))"; done

test:
	cd ../vm && python3 test_vm.py
	python3 ../host/test_tzlow.py
	python3 vm_test.py
	python3 node_test.py
	python3 link_test.py

linktest:
	python3 vm_test.py > /dev/null; python3 link_test.py

run:
	python3 ../host/tzlow.py $(if $(PORT),-p $(PORT),--sim)

flash:
	@echo "flash: not yet (no PICkit2 tool is installed)"; exit 1

clean:
	rm -f $(BUILT)

.PHONY: all size test linktest run flash clean
