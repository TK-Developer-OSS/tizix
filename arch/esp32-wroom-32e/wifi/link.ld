/* WiFi 実験(#109 W1)のリンク。
 *   RAM 部分(.iram / .dram)は esptool の像にして flash 0x1000 から ROM ローダが写す。
 *   flash 直接実行の部分は自分でキャッシュ MMU を張る(main.c cache_map):
 *     .flash.rodata  0x3F400000(DROM)= flash 0x040000 から 192KB
 *     .flash.text    0x400D0000(IROM)= flash 0x070000 から 576KB
 *   この 2 つは objcopy で生の bin にして、それぞれの flash 位置へ別に書く。 */
ENTRY(_start)
PROVIDE ( uart_tx_wait_idle = 0x40009278 );
PROVIDE ( esp_rom_spiflash_read = 0x40062ed8 );
/* ROM の割込み(xtos)。ESP-IDF v5.5 の ld には無く、~/bin/esp32/idf-ld(v6.1)から */
PROVIDE ( _xtos_ints_off = 0x4000bfac );
PROVIDE ( _xtos_ints_on = 0x4000bf88 );
PROVIDE ( _xtos_set_interrupt_handler_arg = 0x4000bf34 );

MEMORY
{
    iram  (rx) : ORIGIN = 0x40080000, LENGTH = 0x20000
    dram  (rw) : ORIGIN = 0x3FFB0000, LENGTH = 0x2C000
    drom  (r)  : ORIGIN = 0x3F400000, LENGTH = 0x30000
    irom  (rx) : ORIGIN = 0x400D0000, LENGTH = 0x90000
}

SECTIONS
{
    .iram : {
        *(.literal.entry) *(.text.entry)
        *start.o(.literal .text)
        *main.o(.literal.cache_map .text.cache_map)
        *(.iram1 .iram1.* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.*)
        *(.phyiram .phyiram.* .rtc.text .rtc.text.*)
        *libgcc.a:*(.literal .text .literal.* .text.*)
        /* クロックの切り替え中は flash の読み出しが揺れうるので、rtc_clk 系は IRAM に
         * (ESP-IDF の esp_hw_support linker.lf も rtc_clk を noflash にしている) */
        *libesp_hw_support.a:rtc_*(.literal .text .literal.* .text.*)
        *libesp_hw_support.a:clk_utils*(.literal .text .literal.* .text.*)
        *libhal.a:*clk*(.literal .text .literal.* .text.*)
    } > iram

    .dram : {
        *(.dram1 .dram1.*)
        *(.rtc.data .rtc.data.* .rtc.force_slow .rtc.force_slow.* .rtc.force_fast .rtc.force_fast.* .rtc.rodata .rtc.rodata.*)
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > dram

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(16);
        _bss_end = .;
        . = . + 0x2000;
        _stack_top = .;
        _heap_start = .;
    } > dram
    _heap_end = ORIGIN(dram) + LENGTH(dram);

    .flash.rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.rodata_wlog*)
        . = ALIGN(4);
    } > drom

    .flash.text : {
        *(.literal .text .literal.* .text.*)
        *(.wifislpiram .wifislpiram.* .wifiorslpiram .wifiorslpiram.* .wifislprxiram .wifislprxiram.* .wifiextrairam .wifiextrairam.*)
    } > irom

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.xt.prop*) *(.xt.lit*) *(.xtensa.info) }
}
