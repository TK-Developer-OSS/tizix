/* user/bigbss.c - 複数スロットのプロセス(#113)の試験用。gcc 系のうち esp32 だけに載せる
 *   (m68k は 1 スロットに像を丸ごと置く作りなので、この大きさの .bss はリンクで止まる)。
 *
 *   bigbss [SEC]   20000 バイトの静的配列を全部書いて読み返し、"bigbss: 20000 ok" を出す。
 *                  SEC を付けるとその秒数だけ眠ってから終わる(ps / free で枠の数を見るため)。
 *   .bss が 16KB を超えるので、ローダーは連続した 2 スロットを取る(2 個目は PID_CONT)。
 *   python/test_mbox.py ではなく python/test_multislot.py が使う。
 */
#include "stdio.h"
#include "mbox.h"

#define N 20000
static unsigned char buf[N];

int main(int argc, char **argv)
{
    unsigned i, bad = 0;
    unsigned long sec = 0;

    for (i = 0; i < N; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    for (i = 0; i < N; i++)
        if (buf[i] != (unsigned char)(i * 7 + 3))
            bad++;
    printf("bigbss: %u %s\n", N, bad ? "BAD" : "ok");
    if (argc >= 1)
        for (i = 0; argv[0][i] >= '0' && argv[0][i] <= '9'; i++)
            sec = sec * 10 + (unsigned long)(argv[0][i] - '0');
    if (sec)
        ksleep(sec * 100);
    return bad ? 1 : 0;
}
