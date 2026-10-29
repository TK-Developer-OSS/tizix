/* tz/drv16.ld : /bin/driver.bin -- the DRIVER at offset 0 of its own segment, code only.
 *   drv_entry (tz/drv16e.S) must be at 0: user/drvc.S far-calls DRIVER:0000.
 *   The DRIVER runs with DS = the caller's segment, so data of its own could not be reached: any .data / .rodata /
 *   .bss stops the link (tzcc puts string literals in .data, so drv16.c must not have any).
 */
OUTPUT_FORMAT(binary)
ENTRY(drv_entry)

SECTIONS
{
  . = 0;
  .text : {
    *(.text.entry)
    *(.text)
    *(.text.*)
  }
  .data : {
    *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.bss) *(.bss.*) *(COMMON)
  }
  ASSERT(SIZEOF(.data) == 0, "driver.bin must have no data, rodata or bss")

  /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
