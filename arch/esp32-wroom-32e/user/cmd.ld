/* arch/esp32-wroom-32e/user/cmd.ld : 外部コマンドのリンカスクリプト
 *
 *   コードとデータを別の枠に置く(DRAM は実行できず、IRAM はバイトで読めない。
 *   loader.c 冒頭の説明を参照)。TBASE / DBASE は Makefile が --defsym で渡す。
 *   同じ .o を番地を変えて 3 回リンクし、tools/mkcmd.py が「番地で変わった語」から
 *   再配置表を作って .bin にまとめる。
 *
 *   リテラル(l32r が引く 32bit の語)は全部コードの先頭に集める。l32r は後ろ向きに
 *   256KB まで届くので、16KB の枠ならどこからでも引ける。入口は先頭に来ないので、
 *   mkcmd.py が ELF の entry を .bin のヘッダに書く。
 */
ENTRY(_start)

SECTIONS
{
    .text TBASE :
    {
        *(.literal .literal.*)
        *(.text.start)
        *(.text .text.*)
        . = ALIGN(4);
    }

    .data DBASE :
    {
        /* 先頭 32B はプロセスの見出し(src/phdr.h。眠り / 起きる時刻の欄)。0 で空けておく */
        . = 0x20;
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(4);
    }

    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
    }

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) *(.xt.prop*) *(.xt.lit*) *(.xtensa.info) }
}
