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
  /* Top of the kernel stack. KSTK_TOP given (ld --defsym, Makefile.tz): the stack is in segment 0, below the kernel
   * (physical 0x600..KSTK_TOP, between the BIOS data and the kernel at the 4KB boundary). Not given (the gcc kernel):
   * the top of the 64KB window as before. */
  __kstack_top = DEFINED(KSTK_TOP) ? KSTK_TOP : 0xFFFE;

  /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
