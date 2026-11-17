/* arch/rv32-fpga-papiliolx6 link-kernel.ld: RAM at 0x80000000 (XBUS) */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { ram (rwx) : ORIGIN = 0x80000000, LENGTH = 0x100000 }  /* plat.h: the next slot is at 0x80100000 */
SECTIONS {
  .text : { *(.text.start) *(.text*) *(.rodata*) *(.srodata*) } > ram
  .data : { *(.data*) *(.sdata*) } > ram
  .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram);
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.riscv.attributes) }
}
