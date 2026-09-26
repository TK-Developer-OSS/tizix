/* user/netcli.c - netcli.h の実装。net.bin(常駐デーモン)の共有リング
 *   バッファ(kmem.h の KW_NET*)を直接読み書きするだけ。プロトコルの
 *   解釈は一切しない(host/port を渡して繋ぐのと、生バイトの読み書きのみ)。 */
#include "kmem.h"
#include "netcli.h"

extern unsigned int getticks(void);

#define NB(a)  (*(volatile unsigned char *)(a))

int net_connect(const char *hostport)
{
    unsigned char i;
    unsigned int t0;

    if (NB(KW_NETSTATE) != NETSTATE_IDLE)
        return -1;                      /* 前の接続がまだ片付いていない */

    for (i = 0; i < KW_NETHOST_MAX - 1 && hostport[i]; i++)
        NB(KW_NETHOST + i) = (unsigned char)hostport[i];
    NB(KW_NETHOST + i) = 0;

    NB(KW_NETCMD) = NETCMD_CONNECT;

    t0 = getticks();
    while (NB(KW_NETSTATE) == NETSTATE_IDLE) {
        if ((unsigned)(getticks() - t0) > 500)
            return -1;                  /* net.bin が居ない(~5秒でタイムアウト) */
    }
    return (NB(KW_NETSTATE) == NETSTATE_CONNECTED) ? 0 : -1;
}

int net_read(char *buf, int max)
{
    int n = 0;
    unsigned char h, t;

    while (n < max) {
        h = NB(KW_NETRXH);
        t = NB(KW_NETRXT);
        if (h == t) break;              /* データ無し */
        buf[n++] = (char)NB(KW_NETRXBUF + t);
        t++; if (t >= KW_NETBUF_SIZE) t = 0;
        NB(KW_NETRXT) = t;
    }
    return n;
}

int net_write(const char *buf, int len)
{
    int n = 0;
    unsigned char h, t, nh;

    while (n < len) {
        h = NB(KW_NETTXH);
        t = NB(KW_NETTXT);
        nh = h + 1; if (nh >= KW_NETBUF_SIZE) nh = 0;
        if (nh == t) break;             /* 送信バッファ満杯 */
        NB(KW_NETTXBUF + h) = (unsigned char)buf[n++];
        NB(KW_NETTXH) = nh;
    }
    return n;
}

void net_close(void)
{
    NB(KW_NETCMD) = NETCMD_CLOSE;
}
