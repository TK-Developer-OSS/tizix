OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { rom (rx) : ORIGIN = 0x00000000, LENGTH = 0x1000 }
SECTIONS {
  .text : { *(.text.start) *(.text*) *(.rodata*) *(.srodata*) } > rom
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.riscv.attributes) *(.data*) *(.bss*) *(.sdata*) *(.sbss*) }
}
