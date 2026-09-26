/* arch/x86-ia16/user/cmd.ld : ユーザーコマンドのリンカスクリプト
 *   .BIN は pseg:0x0100 起点(.COM 流儀)。crt0cmd の _start を必ず先頭へ。
 *   VMA = 0x100(シンボル解決用)/ LMA = 0(--oformat binary のファイル配置。
 *   先頭ゼロパディングを避ける)。
 */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
  .text 0x100 : AT(0)
  {
    *(.text.start)
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
    *(.gnu.linkonce.r.*)
    *(.data)
    *(.data.*)
  }
  . = ALIGN(2);
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) }
  . = ALIGN(2);
  __bss_end = .;

  /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}
