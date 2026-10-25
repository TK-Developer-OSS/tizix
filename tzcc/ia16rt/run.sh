#!/bin/bash
# ia16rt/run.sh <file.c> - compile with tzcc --march=ia16, run in QEMU (SS != DS), print the serial output.
set -u
here=$(cd "$(dirname "$0")" && pwd)
T=$here/../tzcc
W=${IA16_WORK:-$(mktemp -d)}
c=$1; n=$(basename "$c" .c)
PATH=$HOME/tools/ia16/bin:$PATH
"$T" --march=ia16 -o "$W/$n.s" "$c" 2>"$W/$n.terr" || { echo "TZCC-FAIL"; exit 2; }
ia16-elf-as "$W/$n.s" -o "$W/$n.o" 2>"$W/$n.aerr" || { echo "AS-FAIL: $(head -1 "$W/$n.aerr")"; exit 2; }
ia16-elf-as "$here/rt.s" -o "$W/rt.o"
ia16-elf-ld -Ttext=0 -e _rt_start --oformat binary -o "$W/$n.bin" "$W/rt.o" "$W/$n.o" 2>"$W/$n.lerr" || { echo "LD-FAIL: $(head -1 "$W/$n.lerr")"; exit 2; }
secs=$(( ($(stat -c%s "$W/$n.bin") + 511) / 512 ))
ia16-elf-as --defsym NSECT=$secs "$here/boot.s" -o "$W/boot.o"
ia16-elf-ld -Ttext=0x7c00 -e _start --oformat binary -o "$W/boot.bin" "$W/boot.o"
dd if=/dev/zero of="$W/$n.img" bs=1k count=1440 status=none
dd if="$W/boot.bin" of="$W/$n.img" conv=notrunc status=none
dd if="$W/$n.bin" of="$W/$n.img" bs=512 seek=1 conv=notrunc status=none
timeout 10 qemu-system-i386 -drive file="$W/$n.img",format=raw,if=floppy -boot a \
   -device isa-debug-exit,iobase=0xf4,iosize=0x04 -serial file:"$W/$n.out" \
   -display none -no-reboot >/dev/null 2>&1
cat "$W/$n.out" 2>/dev/null
