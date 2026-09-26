/* arch/m68k-mega link-kernel.ld
 * SRAM がそのまま ROM(Mega が起動前に書き込む)なので、ベクタ+コード+
 * 初期化データを全部このイメージに収め、0x000000 起点でリンクする。
 * .bss は crt0.s が起動時に自前でゼロクリアする(イメージには含めない)。
 */

MEMORY
{
    ram (rwx) : ORIGIN = 0x000000, LENGTH = 0x100000  /* plat.h RAM_SIZE(1MB)と一致させる */
}

SECTIONS
{
    .vectors 0x000000 : { *(.vectors) } > ram

    .text : {
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

    __stack_top = ORIGIN(ram) + LENGTH(ram);

    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
