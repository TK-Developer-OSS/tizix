/* arch/rv32-fpga-papiliolx6/user/cmd.ld : linker script for external commands (CBASE is passed with --defsym)
 *   Link the same .o twice with different CBASE; tools/mkcmd.py builds the relocation table from the words that differ. */
ENTRY(_start)
SECTIONS
{
	. = CBASE;
	.text : {
		. = . + 0x20;		/* the first 32 bytes are the process header (src/phdr.h) */
		*(.text.start)
		*(.text .text.*)
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
		*(.data .data.*)
		*(.sdata .sdata.*)
		. = ALIGN(4);
	}
	__bss_start = .;
	.bss (NOLOAD) : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) }
	. = ALIGN(4);
	__bss_end = .;
	/DISCARD/ : { *(.comment) *(.eh_frame) *(.riscv.attributes) *(.note.*) }
}
/* image + bss must fit in IMG_BUDGET (0x8000) of loader.c. If it overflows, the bss clear wipes argv */
ASSERT(__bss_end - CBASE <= 0x8000, "image+BSS exceeds IMG_BUDGET (0x8000). See loader.c")
