/* user/espuart.h - ESP-WROOM-02 を bit-bang UART(9600bps 8N1 + CTS)で繋ぐ物理層(#62)
 *   実体は arch/z80board/espuart.s(手書き PIC、net.bin にリンク)。
 *   ビット割り当て(SD と同じラッチ 0x80 に相乗り、2026-09-23 TK 設計・実配線済み):
 *     OUT bit0 = TX → ESP GPIO3(RXD) / bit1 = CTS → ESP GPIO13(0 = 送ってよい)
 *         bit4 = ~RST → ESP EXT_RSTB
 *     IN  bit0 = RX ← ESP GPIO1(TXD)
 *   ESP 側は純正 ESP-AT(9600bps に恒久設定済み)。CTS フロー制御は起動時に
 *   netesp.c が AT+UART_CUR で有効にする(保存しない)。
 *   以前の SPI 版(espspi.s / espat.h / esp/tzesp_at)は使わなくなった(ファイルは残してある)。
 */
#ifndef ESPUART_H
#define ESPUART_H

void esp_rst(int on) __sdcccall(0);     /* on != 0 → ~RST Low(リセット保持) */
void esp_tx(int c) __sdcccall(0);       /* 1 バイト送信 */
/* CTS=0 にして最大 max バイト受信。最初の 1 バイトは wait 回(1 回 = 48T ≒ 6us)まで待つ。
 * 戻り = 受信バイト数。戻るときは CTS=1。 */
int  esp_rxbuf(unsigned char *buf, int max, int wait) __sdcccall(0);

#define ESP_RXBUFLEN   32       /* esp_rxbuf 1 回の上限(netesp.c の rbuf) */

#endif
