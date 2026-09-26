#ifndef _HW_H
#define _HW_H

/* I/O Port Definitions */
#define CON_PORT      0x01
#define SPI_PORT      0x80
#define SYS_STAT_PORT 0x10

/* ---- SPI_PORT(0x80)のビット割り当て(SD と ESP-WROOM-02 が相乗り) --------
 *   #62(2026-09-23 TK 設計・実配線済み)で ESP は bit-bang UART になり、SD と
 *   同じラッチ(OUT = 74HC574 / IN = 74HC541)の空きビットを使う:
 *     OUT: bit7 = MOSI(SD)/ bit6 = CS(SD、1 = 非選択)/ bit5 = SCK(SD)
 *          bit4 = ~RST(ESP)/ bit1 = CTS(ESP、0 = 送ってよい)/ bit0 = TX(ESP の RXD)
 *     IN : bit7 = MISO(SD)/ bit0 = RX(ESP の TXD)
 *   SD 側(spi.s)は bit0-4 を常に 1(TX マーク・CTS 止め・~RST 解除)で出すこと。
 *   以前 spi_close が 0x40 を出していて、SD を閉じるたびに ESP をリセットしていた。
 *   UART 本体は arch/z80board/espuart.s(9600bps 8N1 + CTS)。
 *
 *   ESP_PORT(0x81)は SPI 版(espspi.s + ESP 側の自作ファーム)の名残で、今は
 *   使っていない。実機の 74HC138 デコード(A4〜A6 のみ)では 0x80 と同じ選択線。
 */
#define ESP_PORT      0x81

#endif
