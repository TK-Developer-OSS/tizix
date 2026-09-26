/* tizix x86-ia16 : カーネルリンカスクリプト (M1)
 *   カーネルは 0x1000:0000 にロードされ、CS=DS=SS=0x1000 の 64KB
 *   セグメント1本に text/rodata/data/bss/stack を収める(small モデル)。
 *   オフセットは 0 起点。フラットバイナリで出力。
 */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
  . = 0x0000;

  .text : {
    *(.text._start)     /* crt0 の _start を必ず先頭に */
    *(.text)
    *(.text.*)
  }

  .rodata : {
    *(.rodata)
    *(.rodata.*)
    *(.gnu.linkonce.r.*)
  }

  .data : {
    *(.data)
    *(.data.*)
  }

  . = ALIGN(2);
  __bss_start = .;
  .bss : {
    *(.bss)
    *(.bss.*)
    *(COMMON)
  }
  . = ALIGN(2);
  __bss_end = .;

  /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
