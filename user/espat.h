/* user/espat.h - ESP-WROOM-02(WiFi)を bit-bang SPI で叩くときの取り決め。
 *
 *   z80board(実機 Z80 ボード)専用。SD カードの次の I/O ポート(hw.h の
 *   ESP_PORT、暫定 0x81)に 74HC574 + 74HC541 をもう 1 組ぶら下げ、
 *   そこへ ESP-WROOM-02 を SPI スレーブとして接続する前提。
 *
 *   ■ なぜ独自フレーミングが要るのか
 *   ESP8266(ESP-WROOM-02)の純正 ESP-AT ファームは **UART 専用** で、
 *   SPI 越しの AT インタフェースを持たない(SPI/SDIO の AT は ESP32 系のみ)。
 *   したがって ESP 側にも自前ファームを載せる必要がある。tizix では
 *   arch/z80board/esp/tzesp_at/tzesp_at.ino が
 *     「SPI スレーブ(下記フレーミング) ⇔ ESP-AT 互換のコマンド解釈 + TCP」
 *   を担当する。Z80 側が話す言葉は **本物の ESP-AT と同じ文字列** なので、
 *   python/at_modem.py(ESP-AT テストベッド)をそのまま相手にできる
 *   = シミュレータ(z80boardsim)で同じコードを検証できる。
 *
 *   ■ フレーミング(ESP8266 HSPI スレーブ = Arduino SPISlave の形)
 *   1 トランザクション = CS Low → CMD(1B) → ADDR(1B, 0 固定)
 *                        → [読み出し系のみ DUMMY(1B)] → データ → CS High。
 *   データ長は「データバッファ 32B」「ステータス 4B」に固定(ハードウェアの
 *   バッファ長)。CMD の値は ESP8266 の SPI スレーブが持つ 4 種:
 *     0x01 = WRSTA(master → slave, 4B)   ※tizix では未使用
 *     0x02 = WRBUF(master → slave, 32B)
 *     0x03 = RDBUF(slave → master, 32B)
 *     0x04 = RDSTA(slave → master, 4B)
 *   ★この 4 値と DUMMY の有無は ESP8266 core(hspi_slave.c)の実装に
 *     従うもの。**実機で合わなければここを直す**(Z80 側・iosim.c 側・
 *     .ino 側の 3 箇所が同じ定義を使う)。
 *
 *   ■ データバッファの中身(32B のうち)
 *     [0]      = 有効バイト数 n(1..31。0 = 中身無し)
 *     [1..n]   = ペイロード(AT コマンド文字列 / 応答 / TCP 生データ)
 *   ■ ステータス 4B(RDSTA、送出順)
 *     [0] = ESP_MAGIC(0x5A) … リンク生存の目印。これが無ければ配線/ファーム
 *                              が来ていないと判断できる(ブリングアップ用)
 *     [1] = フラグ(ESP_ST_*)
 *     [2] = 受信待ちバイト数の目安(0 でも [1] の bit0 が真なら読む)
 *     [3] = 0
 *     ※ ESP 側は uint32 で status を書くため、実機でバイト順が逆に出る
 *       可能性がある。Z80 側(netesp.c esp_stat)は [0] と [3] の両方で
 *       マジックを探し、逆順でも動くようにしてある。
 */
#ifndef ESPAT_H
#define ESPAT_H

/* ---- SPI トランザクションのコマンド ---- */
#define ESP_CMD_WRSTA   0x01
#define ESP_CMD_WRBUF   0x02
#define ESP_CMD_RDBUF   0x03
#define ESP_CMD_RDSTA   0x04

#define ESP_BUFLEN      32      /* データバッファ長(ハード固定) */
#define ESP_STALEN      4       /* ステータス長(ハード固定) */
#define ESP_PAYMAX      31      /* 1 トランザクションで運べるバイト数 */

#define ESP_MAGIC       0x5A    /* ステータスの目印 */
#define ESP_ST_RXRDY    0x01    /* slave → master のデータが RDBUF に有る */
#define ESP_ST_TXRDY    0x02    /* slave が WRBUF を受け取れる */

/* ---- 物理層(arch/z80board/espspi.s、手書き PIC アセンブラ)----
 *   ポート出力の bit0 は ESP の ~RST(0 = リセット)に割り当ててある。
 *   ラッチの bit0-4 はもともと未使用で、SD 側(spi.s)も 1 を出しっぱなし
 *   にしていた枠。ここを使う理由は ESP8266 のブートストラップ:
 *   **HSPI スレーブの CS は GPIO15 固定で、GPIO15 は起動時に Low でないと
 *   ESP が起動しない**。Z80 側のラッチは CS = High(非選択)で待機するので、
 *   電源投入順によっては ESP が永久に立ち上がらない。esp_rst() で
 *   「CS を Low にしてから ~RST を Low → High」とやれば必ず正しい状態で
 *   起動する(netesp.c の esp_boot がこれをやる)。
 *   ★~RST を配線していないハードでも bit0 は誰も見ていないだけなので
 *     害は無い(その場合 ESP の起動順は運任せになる)。 */
void esp_cs(int on) __sdcccall(0);
void esp_rst(int on) __sdcccall(0);     /* on != 0 → リセット状態(~RST Low) */
int  esp_xfer(int tx) __sdcccall(0);

#endif
