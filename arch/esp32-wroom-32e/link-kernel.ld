/* arch/esp32-wroom-32e link-kernel.ld
 * ROM ローダが esptool 形式イメージのセグメントをそのまま写す先。
 * 命令は IRAM、データ(と .rodata。IRAM は 32bit アクセスしか効かないので
 * 文字列を置けない)は DRAM。.bss は crt0.S がゼロにする。
 * 番地は include/plat.h と一致させること。
 *
 * WiFi(#109、PLAT_WIFI)のときは、ライブラリ(.a。Espressif のバイナリと ESP-IDF のビルド済み部品)の
 * 命令と読み出し専用データを flash から直接走らせる(IRAM に入らない)。
 *   .flash.text   0x400D0000(IROM)= flash IROM_FLASH_OFF
 *   .flash.rodata 0x3F400000(DROM)= flash DROM_FLASH_OFF
 * この 2 つは ROM ローダの像には入れず、Makefile が objcopy で別に書く(kmain.c がキャッシュ MMU を張る)。
 * カーネル自身(.o)と libgcc は IRAM のまま ── flash の読み書きの間はキャッシュを止めるので、
 * その間に走るもの(diskio・コンソール・割り算の助け)は flash に置けない。
 */
ENTRY(_start)

/* ROM の SPI flash 関数(diskio.c が使う)。番地は ESP-IDF v5.5 の
 * components/esp_rom/esp32/ld/esp32.rom.spiflash_legacy.ld から
 * (写しは rocky9 の ~/bin/esp32/idf-ld/)。ESP-IDF の ld 一式には依存しない。 */
/* diskio.c はこの tizix_ の名前で ROM のものを直接呼ぶ。esp_rom_spiflash_* の名前のままだと、
 * WiFi 入りでは ESP-IDF の libesp_rom.a の差し替え版(flash 上に置かれる)が勝ってしまい、
 * キャッシュを止めた中で呼んで落ちた。 */
tizix_rom_spiflash_read         = 0x40062ed8;
tizix_rom_spiflash_write        = 0x40062d50;
tizix_rom_spiflash_erase_sector = 0x40062ccc;
PROVIDE ( uart_tx_wait_idle             = 0x40009278 );
PROVIDE ( intr_matrix_set               = 0x4000681c );

MEMORY
{
    iram (rx)  : ORIGIN = 0x40080000, LENGTH = 0xC000    /* 命令ブロック 3 つ。残り 80KB はプロセスのコード枠(plat.h PROC_TEXT_BASE) */
    dram (rw)  : ORIGIN = 0x3FFB0000, LENGTH = 0x1C000   /* 残り 80KB はプロセスのデータ枠(PROC_DATA_BASE) */
    drom (r)   : ORIGIN = 0x3F400000, LENGTH = 0x30000   /* plat.h DROM_FLASH_SIZE */
    irom (rx)  : ORIGIN = 0x400D0000, LENGTH = 0x90000   /* plat.h IROM_FLASH_SIZE */
}

SECTIONS
{
    /* ライブラリ(.a)の命令と定数は flash へ(libgcc を除く)。順番が先の規則が勝つので、
     * カーネル側の .text / .data より前に置く。 */
    .flash.text : {
        /* カーネル自身のうち、WiFi が起きて(キャッシュを張って)からしか走らないもの。
         * net/ktls.c は slot0 の knet_poll からだけ呼ばれる。flash の読み書き(キャッシュを止める)は
         * syscall の中で割込みを止めて行うので、その間に slot0 がここを走っていることは無い。 */
        *net/ktls.o(.literal .text .literal.* .text.*)
        *libc.a:*(.literal .text .literal.* .text.*)
        *libm.a:*(.literal .text .literal.* .text.*)
        *libnet80211.a:*(.literal .text .literal.* .text.* .iram1 .iram1.* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifislpiram .wifislpiram.* .wifiorslpiram .wifiorslpiram.* .wifislprxiram .wifislprxiram.* .wifiextrairam .wifiextrairam.*)
        *lib[a-fh-z]*.a:*(.literal .text .literal.* .text.* .iram1 .iram1.* .phyiram .phyiram.* .wifi0iram .wifi0iram.* .wifirxiram .wifirxiram.* .wifislpiram .wifislpiram.* .wifiorslpiram .wifiorslpiram.* .wifislprxiram .wifislprxiram.* .wifiextrairam .wifiextrairam.*)
    } > irom

    .flash.rodata : {
        *lib[a-fh-z]*.a:*(.rodata .rodata.* .srodata .srodata.* .rodata_wlog*)
    } > drom

    .text : {
        /* ベクタ表が先頭(VECBASE は 1KB 境界。crt0.S の .org が表の中の位置を決める) */
        KEEP(*(.vectors))
        /* l32r は後ろ向きにしか届かないので、リテラルを必ず使う側より前に置く
         * (入口は ELF の entry で決まるので、_start が先頭である必要は無い)。 */
        *(.literal.entry)
        *(.text.entry)
        *(.literal .text .literal.* .text.* .iram1 .iram1.*)
    } > iram

    .data : {
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.* .dram1 .dram1.*)
        *(.rtc.data .rtc.data.* .rtc.force_slow .rtc.force_slow.* .rtc.force_fast .rtc.force_fast.* .rtc.rodata .rtc.rodata.*)
        . = ALIGN(4);
    } > dram

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > dram

    __stack_top = ORIGIN(dram) + LENGTH(dram);

    /* WiFi のヒープ(net/osal.c の _sbrk)。DRAM の上の方(SRAM1)。ROM が起動後も使う
     * 0x3FFE0000-0x3FFE0440 と 0x3FFE3F20-0x3FFE4350 を避けて、その上から終わりまで。 */
    _heap_start = 0x3FFE4400;
    _heap_end   = 0x40000000;

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.xt.prop*) *(.xt.lit*) *(.xtensa.info) }
}
