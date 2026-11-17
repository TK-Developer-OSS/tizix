/* rv32-qemu: QEMU virt, RAM at 0x80000000 (-bios none starts here) */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M }
SECTIONS {
  .text : { *(.text.start) *(.text*) } > RAM
  .rodata : { *(.rodata*) } > RAM
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; } > RAM
  . = ALIGN(16);
  . = . + 16K;
  __stack_top = .;
}
