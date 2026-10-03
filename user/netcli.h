/* user/netcli.h - TCP 接続を使うコマンド(telnet / tzftp / ntpdate / atcli / nettest)の API。
 *   使うコマンドは stdio.h の後にこれをインクルードする。
 *
 *   実体はアーキで違うが、コマンドから見た約束は同じ:
 *     z80(SDCC) … net.bin(常駐デーモン、`net &`)と共有リング(kmem.h の KW_NET*)で話す。
 *                  実装は netcli.c(Makefile の該当 .ihx ルールに $(OBJ)/netcli.rel を足してリンク)。
 *                  `net &` がいなければ net_connect はタイムアウトで -1。
 *     gcc(TZ_SYSCALL、esp32-wroom-32e など) … カーネルが TCP を持つ(syscall 36..41、src/knet.h)。
 *                  下の static 関数がそのまま実装。`net &` は要らない。
 *                  ネットワークを持たないアーキ(m68k-mega)では net_connect が -1 を返すだけ。
 */
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

#ifdef TZ_SYSCALL
/* ---- gcc 側: カーネルの syscall(src/sysfile.c の 36..41)---- */

/* net_state() の値(z80 の kmem.h NETSTATE_* と同じ。1 はこちらでは「接続中」) */
#define NET_IDLE        0
#define NET_CONNECTING  1
#define NET_CONNECTED   2
#define NET_ERROR       3
#define NET_LISTEN      4   /* net_listen で待ち受け中 */

/* net_info(sel) の番号(ifconfig / netstat が使う)。アドレスは先頭のオクテットが下位バイト。
 * インタフェースが無い / 起きていないときは NI_FLAGS が 0、ほかは 0xFFFFFFFF。 */
#define NI_FLAGS   0    /* bit0 = 起きている / bit1 = リンク(WiFi 接続)あり / bit2 = DHCP */
#define NI_ADDR    1
#define NI_MASK    2
#define NI_GW      3
#define NI_DNS     4
#define NI_MACHI   5    /* MAC の先頭 2 バイト */
#define NI_MACLO   6    /* MAC の残り 4 バイト */
#define NI_RXPKT   7
#define NI_TXPKT   8
#define NI_MTU     9
#define NI_STATE   16   /* 接続の状態(NET_*) */
#define NI_RADDR   17   /* 相手のアドレス */
#define NI_RPORT   18
#define NI_LPORT   19
#define NI_RXBYTE  20
#define NI_TXBYTE  21
#define NI_TCPST   22   /* lwIP の tcp_state(4 = ESTABLISHED、7 = CLOSE_WAIT …) */
#define NI_RXIN    23   /* lwIP から受けたバイト数(NI_RXBYTE はそのうちリングへ移した数) */
#define NI_PEND    24   /* lwIP から受けて、まだリングへ移していないバイト数 */
#define NI_ARP     32   /* ARP 表: NI_ARP + 番号 * 4 + (0 = IP / 1 = MAC 先頭 2 バイト / 2 = 残り 4 バイト)。
                         * 空きは IP が 0、表の外は 0xFFFFFFFF */

static int net_state(void)
{
        return (int)syscall5(37, 0, 0, 0, 0);
}

static unsigned long net_info(unsigned sel)
{
        return syscall5(41, (unsigned long)sel, 0, 0, 0);
}

/* flags: 0 = 素の TCP / NET_TLS = TLS(証明書を検証)/ NET_TLS|NET_INSECURE = TLS(検証しない) */
#define NET_TLS       1
#define NET_INSECURE  2
static int net_connect_flags(const char *hostport, unsigned flags)
{
        unsigned int t0, lim = flags ? 2500 : 1500;  /* DNS + TCP で ~15 秒、TLS は握手と時計合わせで +10 秒 */
        int st;

        if (syscall5(36, (unsigned long)hostport, (unsigned long)flags, 0, 0) != 0)
                return -1;
        t0 = getticks();
        while ((st = net_state()) == NET_CONNECTING) {
                if ((unsigned)(getticks() - t0) > lim) {
                        syscall5(40, 0, 0, 0, 0);
                        return -1;
                }
        }
        return st == NET_CONNECTED ? 0 : -1;
}

int net_connect(const char *hostport)
{
        return net_connect_flags(hostport, 0);
}

/* 待ち受け(TCP サーバ)。port で 1 本受ける。戻り: 0 = 待ち受けを始めた / -1 = 使用中・ネット無し。
 * 繋がってきたら net_state() が NET_CONNECTED になり、あとは net_read / net_write / net_close。
 * 扱う接続は同時に 1 本。処理している間に来た接続は 3 本まで待たされ、閉じた後でまた呼ぶと
 * 待たせていた順に渡される(MCP のクライアントは依頼を並行して投げる。#110)。
 * 待ち受けは受け手が呼び直す間も開けておき、10 秒呼ばれなければカーネルが閉じる。 */
static int net_listen(unsigned port)
{
        return syscall5(46, (unsigned long)port, 0, 0, 0) == 0 ? 0 : -1;
}

/* TLS で繋ぐ(以後の net_read / net_write は平文のまま使える)。verify = 0 で証明書を検証しない */
static int net_connect_tls(const char *hostport, int verify)
{
        return net_connect_flags(hostport, verify ? NET_TLS : (NET_TLS | NET_INSECURE));
}

int net_read(char *buf, int max)
{
        return (int)syscall5(38, (unsigned long)buf, (unsigned long)max, 0, 0);
}

int net_write(const char *buf, int len)
{
        return (int)syscall5(39, (unsigned long)buf, (unsigned long)len, 0, 0);
}

void net_close(void)
{
        syscall5(40, 0, 0, 0, 0);
}

/* 名前(数字の IP でもよい)を引く。0=引けた(*addr、*ms = かかった ms)/ -1=引けない・ネット無し。
 * 最大 ~10 秒待つ。アドレスは先頭のオクテットが下位バイト(ifconfig / netstat と同じ)。 */
static int net_resolve(const char *name, unsigned long *addr, unsigned long *ms)
{
        unsigned int t0;
        unsigned long st;

        if (syscall5(42, (unsigned long)name, 0, 0, 0) != 0)
                return -1;
        t0 = getticks();
        while ((st = syscall5(43, (unsigned long)addr, (unsigned long)ms, 0, 0)) == 0)
                if ((unsigned)(getticks() - t0) > 1000)
                        return -1;
        return st == 1 ? 0 : -1;
}

/* ICMP echo を 1 つ送る / その応答を見る(0=まだ、1 以上 = 応答までの ms + 1) */
static int net_ping_send(unsigned long addr, unsigned seq, unsigned len)
{
        return (int)syscall5(44, addr, (unsigned long)seq, (unsigned long)len, 0);
}

static unsigned long net_ping_poll(unsigned seq, unsigned long *ttl)
{
        return syscall5(45, (unsigned long)seq, (unsigned long)ttl, 0, 0);
}

/* アドレスを "a.b.c.d" で出す */
static void net_pr_ip(unsigned long a)
{
        printf("%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
               (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
}
#endif

#endif
