/* user/sleep.c - 外部コマンド sleep
 *   sleep SEC : 指定した秒数だけ待つ。引数は 10 進秒。
 *
 *   実装メモ: 生 tick は 100Hz(src/kernel.c: TICKS++ @ 100Hz)。
 *   カーネルベクタ getticks()(0x0044、Makefile FIXED_SYMS で全コマンドに
 *   供給)で読む。旧版は 0x8520 を "uptime" と決め打ちしていたが、実際の
 *   tick カウンタは KW_TICKS=0x8527 で 0x8520 は別フィールド。値が動かず
 *   busy-wait が抜けずハングしていた(#24 Gemini 版の不具合)。
 *   tick は 16bit ラップするので差分 (now - start) を unsigned で見る。
 */
#include "stdio.h"

extern unsigned int getticks(void) __sdcccall(0);

static unsigned int atou(const char *s)
{
    unsigned int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10u + (unsigned int)(*s - '0');
        s++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *arg;
    unsigned int sec, ticks, i, start;

    (void)argc;
    arg = (argv && argv[0] && argv[0][0]) ? argv[0] : 0;
    if (arg == 0) {
        puts("sleep: usage: sleep <sec>");
        return 1;
    }

    sec = atou(arg);

    /* ticks = sec * 100(100Hz)。桁溢れは ~655 秒超で自然ラップ。*/
    ticks = 0;
    for (i = 0; i < sec; i++)
        ticks += 100u;

    start = getticks();
    while ((unsigned int)(getticks() - start) < ticks)
        ;                       /* busy-wait(割り込み有効なので tick は進む)*/

    return 0;
}
