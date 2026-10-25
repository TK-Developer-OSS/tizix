#!/bin/bash
# tz/check.sh - build the mixed (ia16-gcc + tzcc) kernel and boot it in QEMU; expect the shell prompt.
#   usage: [MEMCAP_KB=192] [TZ_QEMU_M=1] tz/check.sh [extra expected string ...]
#   MEMCAP_KB: software limit on the conventional memory the kernel sees (QEMU cannot go below 640KB); TZ_QEMU_M: qemu -m (MB)
cd "$(dirname "$0")/.."
export PATH=$HOME/tools/ia16/bin:$PATH
# kwork[] size: the Makefile.tz formula must equal X86_KWORK_SIZE from src/kmem.h
want=$(printf '#include "kmem.h"\nX86_KWORK_SIZE\n' | ia16-elf-gcc -E -P -mcmodel=tiny -ffreestanding -DX86_MAXSLOT=${TZ_MAXSLOT:-20} -DARCH_X86_IA16 -DFAT12_MIN -DX86_EXTSH -I. -I../../src -I../../src/libc -include plat.h -x c - | tail -1 | sed 's/^/echo $((/;s/$/))/' | sh)
got=$(make -f Makefile.tz OBJ=obj/tz TZ_MAXSLOT=${TZ_MAXSLOT:-20} -pn 2>/dev/null | sed -n 's/^KWORK_SIZE := //p')
[ "$want" = "$got" ] || { echo "KWORK_SIZE mismatch: kmem.h=$want Makefile.tz=$got"; exit 1; }
make -f Makefile.tz OBJ=obj/tz IMG=disk/floppy_tz.img MEMCAP_KB=${MEMCAP_KB:-0} TZ_MAXSLOT=${TZ_MAXSLOT:-20} -j4 >obj/tz.build.log 2>&1 || { echo "BUILD FAIL"; grep -E "error|Error|\*\*\*|undefined" obj/tz.build.log | head -5; exit 1; }
out=$(mktemp)
timeout 8 qemu-system-i386 -m ${TZ_QEMU_M:-128} -drive file=disk/floppy_tz.img,format=raw,if=floppy \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 -serial file:$out -display none -no-reboot -boot a </dev/null >/dev/null 2>&1
tr -d '\r' <$out | head -12 | sed 's/^/  | /'
ok=1
for pat in "tizix" "[/root]#" "$@"; do grep -qF -- "$pat" $out || { echo "MISSING: $pat"; ok=0; }; done
rm -f $out
[ $ok = 1 ] && echo "BOOT OK" || { echo "BOOT FAIL"; exit 1; }
