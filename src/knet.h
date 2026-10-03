/* src/knet.h -- ネットワーク(TCP クライアント 1 本)のカーネル側の口(PLAT_FLAT32 の syscall 36..41)
 *
 *   コマンド側の約束は z80 と同じ user/netcli.h(net_connect / net_read / net_write / net_close)。
 *   z80 では常駐デーモン net.bin が共有リング(kmem.h の KW_NET*)を中継するが、
 *   フラットなアーキでは arch 側のカーネルが TCP を持ち、sysfile.c がここを呼ぶ。
 *   実体は arch 側(esp32-wroom-32e: net/knet.c)。持たないアーキは PLAT_NET を定義せず、
 *   sysfile.c が 0xFFFFFFFF を返す(net_connect が失敗するだけ)。
 *
 *   どの関数もコマンドの syscall(そのコマンドのスロット)から呼ばれる。TCP スタックそのものには
 *   触らず、要求の受け付けとリングの読み書きだけをする(スタックを回すのは arch の側)。
 *
 *   状態(knet_state)は kmem.h の NETSTATE_* と同じ値: 0=idle / 1=接続中 / 2=connected / 3=error。
 */
#ifndef KNET_H
#define KNET_H

/* flags: KNET_TLS = TLS を被せる(証明書を検証)/ KNET_TLS|KNET_INSECURE = TLS だが検証しない(curl -k)。
 * TLS は arch 側が持つときだけ(持たなければ接続が error になる)。 */
#define KNET_TLS       1
#define KNET_INSECURE  2
int           knet_connect(const char *hostport, unsigned flags);   /* 0=受け付けた / -1=使用中・ネット無し */
int           knet_listen(unsigned port);           /* 0=待ち受け開始 / -1。1 本受けたら CONNECTED(syscall 46) */
int           knet_state(void);                     /* 0 idle / 1 接続中 / 2 connected / 3 error / 4 待ち受け中 */
int           knet_read(char *buf, int max);         /* 非ブロッキング。取り出したバイト数 */
int           knet_write(const char *buf, int len);  /* 非ブロッキング。積めたバイト数 */
void          knet_close(void);
unsigned long knet_info(unsigned sel);               /* ifconfig / netstat 用(番号は user/netcli.h) */

/* 名前引き(dig / ping / curl)と ICMP echo(ping)。syscall 42..45。どれも同時に 1 件。
 * アドレスは先頭のオクテットが下位バイト。 */
int           knet_dns_start(const char *name);                       /* 0=受け付けた / -1=ネット無し */
int           knet_dns_poll(unsigned long *addr, unsigned long *ms);  /* 0=引いている / 1=引けた / 2=引けない */
int           knet_ping_send(unsigned long addr, unsigned seq, unsigned len);
unsigned long knet_ping_poll(unsigned seq, unsigned long *ttl);       /* 0=まだ / 応答までの ms + 1 */

#endif
