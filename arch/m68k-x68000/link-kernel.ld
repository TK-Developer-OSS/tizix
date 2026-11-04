/* arch/m68k-x68000 link-kernel.ld
 * boot_ipl.s loads TIZIX.SYS (= kernel.bin) at $10000 and jumps to it.  The ROM's vector
 * table (address 0) is kept, so the image has no vectors (crt0.s replaces the few it needs).
 * crt0.s clears .bss itself at start (it is not part of the image).
 */

MEMORY
{
    ram (rwx) : ORIGIN = 0x010000, LENGTH = 0xF0000  /* plat.h RAM_ORIGIN..RAM_END */
}

SECTIONS
{
    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    } > ram

    .data : {
        *(.data*)
    } > ram

    _bss_start = .;
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } > ram
    _bss_end = .;

    /* Kernel stack: from plat.h's STACK_TOP down (above the highest slot) */
    __stack_top = 0x0FF000;

    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
