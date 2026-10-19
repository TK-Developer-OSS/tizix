/* user/wifi.c - ESP-WROOM-02 へ AT コマンドを 1 行送って応答を表示する
 *   (z80board 専用。net.bin = user/netesp.c が常駐している前提)。
 *
 *   net.bin は TCP の中継しかしないので、AP への参加や IP の確認には
 *   「AT コマンドを 1 行だけ通す」経路が要る。それが kmem.h の
 *   NETCMD_ATCMD で、コマンド本文は **TX リング**(KW_NETTXBUF、127B まで)
 *   に積んで渡し、応答は RX リングへ生で返ってくる。
 *   (KW_NETHOST は 40B しかなく SSID+パスワードが入らないため。)
 *
 *   使い方:
 *     wifi                      … AT+CIFSR(IP アドレスの確認)
 *     wifi <ssid> <password>    … AT+CWJAP="ssid","password"(AP へ参加)
 *     wifi AT+GMR               … 任意の AT コマンドをそのまま送る
 *
 *   ESP-AT は CWJAP の結果をフラッシュに覚えるので、参加は一度やれば
 *   次の電源投入から自動で繋がる(純正 ESP-AT と同じ挙動を
 *   arch/z80board/esp/tzesp_at にも入れてある)。
 */
#include "stdio.h"
#include "string.h"
#include "kmem.h"

extern unsigned int getticks(void);

#define NB(a)  (*(volatile unsigned char *)(a))

#define T_LIMIT   700           /* net.bin の応答待ち 7 秒 */
#define T_TAIL    40            /* 余韻(遅れて来る行)0.4 秒 */

static int tx_put(int c)
{
    unsigned char h, nh;

    h = NB(KW_NETTXH);
    nh = h + 1;
    if (nh >= KW_NETBUF_SIZE)
        nh = 0;
    if (nh == NB(KW_NETTXT))
        return 0;                       /* 満杯 */
    NB(KW_NETTXBUF + h) = (unsigned char)c;
    NB(KW_NETTXH) = nh;
    return 1;
}

/* RX リングに来ている分をそのまま表示する */
static void drain(void)
{
    unsigned char t;

    for (;;) {
        t = NB(KW_NETRXT);
        if (t == NB(KW_NETRXH))
            return;
        putchar((int)NB(KW_NETRXBUF + t));
        t++;
        if (t >= KW_NETBUF_SIZE)
            t = 0;
        NB(KW_NETRXT) = t;
    }
}

int main(int argc, char **argv)
{
    char cmd[100];
    char *p;
    unsigned int t0;

    if (argc >= 2 && argv[0] && argv[1]) {
        strcpy(cmd, "AT+CWJAP=\"");
        strcat(cmd, argv[0]);
        strcat(cmd, "\",\"");
        strcat(cmd, argv[1]);
        strcat(cmd, "\"");
    } else if (argc >= 1 && argv[0]) {
        strcpy(cmd, argv[0]);           /* 生の AT コマンド */
    } else {
        strcpy(cmd, "AT+CIFSR");
    }

    if (NB(KW_NETSTATE) != NETSTATE_IDLE) {
        puts("wifi: 接続中(または前の接続が片付いていない)\n");
        return 1;
    }

    /* 直前の残骸を捨ててからコマンド本文を積む */
    NB(KW_NETTXT) = NB(KW_NETTXH);
    p = cmd;
    while (*p) {
        if (!tx_put((int)*p))
            break;
        p++;
    }

    puts(">> ");
    puts(cmd);
    puts("\n");

    NB(KW_NETCMD) = NETCMD_ATCMD;

    t0 = getticks();
    while (NB(KW_NETCMD) == NETCMD_ATCMD) {
        drain();
        if ((unsigned int)(getticks() - t0) > T_LIMIT) {
            puts("\nwifi: net デーモンが応答しない(`net &` は起動している?)\n");
            return 1;
        }
    }

    t0 = getticks();
    while ((unsigned int)(getticks() - t0) < T_TAIL)
        drain();
    putchar('\n');
    return 0;
}
