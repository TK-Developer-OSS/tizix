/* arch/rpi-pico/user/cmd.ld : 外部コマンドのリンカスクリプト (ARM Cortex-M0+) */
ENTRY(_start)

SECTIONS
{
    .text 0 :
    {
        /* 先頭 32B はプロセスの見出し (src/phdr.h) */
        . = 0x20;
        *(.text.start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
    }
    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) *(.ARM.attributes) }
}

ASSERT(__bss_end - ADDR(.text) <= 0x7800,
       "image+BSS exceeds IMG_BUDGET (0x7800). See loader.c IMG_BUDGET.")


