/* user/net.c - ネットワーク中継の常駐デーモン。`net &` でバックグラウンド
 *   起動する。client socket #1(port 50/51、iosim.c の ATD dial-on-demand)
 *   をポーリングし、KW_NET* の共有リングバッファ(kmem.h)へ素通しで中継する。
 *   プロトコル解釈はしない(host/port の指定と TCP バイト列の中継のみ)。
 *   詳しいプロトコルは kmem.h のコメント参照。
 *
 *   終了させたい時は sh から `kill <block>`。次に使うときは `net &` を
 *   もう一度起動すればよい(起動のたびに状態を初期化するので、それが
 *   そのまま復旧手順になる)。 */
#include "kmem.h"

#define NET_STAT 50
#define NET_DATA 51
__sfr __at NET_STAT NETSTAT;
__sfr __at NET_DATA NETDATA;

extern unsigned int getticks(void);

#define NB(a)  (*(volatile unsigned char *)(a))

/* ATD ダイヤル文字送信。書込み可(bit1)になるまで待つ(telnet.c と同じ)。
 * ここが詰まるのは iosim.c 側の異常系のみなので、長めのタイムアウトで
 * 諦めて次の周回へ戻る(デーモンが永久に固まらないようにする)。 */
static void net_putc(char c)
{
    unsigned int t0 = getticks();
    while (!(NETSTAT & 0x02)) {
        if ((unsigned)(getticks() - t0) > 500)
            return;
    }
    NETDATA = c;
}

int main(int argc, char **argv)
{
    unsigned char h, t, nh;

    (void)argc;
    (void)argv;

    /* 起動のたびに状態を初期化する。kill で刈られた後の再実行が
     * そのまま復旧手順になる(kmem.h のコメント参照)。 */
    NB(KW_NETCMD)   = NETCMD_NONE;
    NB(KW_NETSTATE) = NETSTATE_IDLE;
    NB(KW_NETRXH) = 0; NB(KW_NETRXT) = 0;
    NB(KW_NETTXH) = 0; NB(KW_NETTXT) = 0;

    for (;;) {
        /* --- connect 要求の処理 --- */
        if (NB(KW_NETSTATE) == NETSTATE_IDLE && NB(KW_NETCMD) == NETCMD_CONNECT) {
            unsigned char i;

            net_putc('A'); net_putc('T'); net_putc('D');
            for (i = 0; i < KW_NETHOST_MAX; i++) {
                char c = (char)NB(KW_NETHOST + i);
                if (!c) break;
                net_putc(c);
            }
            net_putc('\n');

            NB(KW_NETRXH) = 0; NB(KW_NETRXT) = 0;
            NB(KW_NETTXH) = 0; NB(KW_NETTXT) = 0;
            /* iosim.c の connect() は上の '\n' を受けた時点で同期的に完了
             * している(成否を伝えるビットは無い)ので、ここでは楽観的に
             * 接続済み扱いにする。呼び出し側は応答の有無/タイムアウトで
             * 実際の成否を判断すること。 */
            NB(KW_NETSTATE) = NETSTATE_CONNECTED;
            NB(KW_NETCMD) = NETCMD_NONE;
        }

        /* --- 中継 --- */
        if (NB(KW_NETSTATE) == NETSTATE_CONNECTED) {
            if (NETSTAT & 0x01) {              /* RX: ソケット -> リング */
                h = NB(KW_NETRXH);
                nh = h + 1; if (nh >= KW_NETBUF_SIZE) nh = 0;
                t = NB(KW_NETRXT);
                if (nh != t) {
                    NB(KW_NETRXBUF + h) = (unsigned char)NETDATA;
                    NB(KW_NETRXH) = nh;
                } else {
                    (void)NETDATA;              /* 満杯: 読み捨てる */
                }
            }

            t = NB(KW_NETTXT);
            h = NB(KW_NETTXH);
            if (t != h && (NETSTAT & 0x02)) {   /* TX: リング -> ソケット */
                NETDATA = NB(KW_NETTXBUF + t);
                t++; if (t >= KW_NETBUF_SIZE) t = 0;
                NB(KW_NETTXT) = t;
            }

            if (NB(KW_NETCMD) == NETCMD_CLOSE) {
                NB(KW_NETSTATE) = NETSTATE_IDLE;
                NB(KW_NETCMD) = NETCMD_NONE;
            }
        }
    }
    return 0;
}
