/* user/netcli.h - net.bin(常駐デーモン、`net &`)と話すクライアント側 API。
 *   TCP 接続を使いたい外部コマンド(wget/ftp 等)はこれをインクルードし、
 *   Makefile の該当 .ihx ルールに $(OBJ)/netcli.rel を足してリンクする
 *   (telnet の LIBRELS_test1/string.rel と同じ流儀)。
 *
 *   前提: `net &` がどこかのブロックで常駐していること。いなければ
 *   net_connect はタイムアウトで -1 を返す。 */
#ifndef NETCLI_H
#define NETCLI_H

/* "host:port" 文字列(NUL 終端)を渡して接続する。戻り: 0=成功 / -1=失敗
 * (net.bin 未起動、タイムアウト、host 側 connect 失敗のいずれか)。
 * 同時に開けるのは 1 接続のみ ── 前の接続がまだ idle に戻っていなければ
 * 即座に -1 を返す。 */
int net_connect(const char *hostport);

/* 受信リングバッファから最大 max バイトを buf へ取り出す(非ブロッキング)。
 * 戻りは実際に取り出せたバイト数(0 なら今は届いていない)。 */
int net_read(char *buf, int max);

/* 送信リングバッファへ最大 len バイトを積む(非ブロッキング、net.bin が
 * 背景で吐き出す)。戻りは実際に積めたバイト数(バッファ満杯なら len 未満)。
 * 呼び出し側は戻り値が len に満たない場合、残りを次回に回すこと。 */
int net_write(const char *buf, int len);

/* net.bin へ close 要求を出す(簡易)。ホスト側ソケットを即座に閉じる
 * 保証は無い ── 実際の切断はサーバ側が閉じるのを待つのが基本(iosim.c の
 * dial-on-demand が peer close を検出して自動的に次の ATD を受け付ける)。 */
void net_close(void);

#endif
