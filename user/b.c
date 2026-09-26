/* b.c - prints 'B' every ~0.5s, forever, until killed.
 *   printf は stdio.h 経由(DRIVER → kputchar)。getticks はカーネル
 *   ベクタ(0x0044)を直接 import。
 *   ※ unsigned 統一(符号付き int は jp PO/M を生み、iy_reg 変換と相性が悪い) */
#include "stdio.h"
extern unsigned int getticks(void);                  /* import 0x0044 */

static void delay_ticks(unsigned int n)
{
    unsigned int t0 = getticks();
    for (;;) {
        unsigned int elapsed = getticks() - t0;
        if (elapsed >= n) break;
    }
}

void main(void)
{
    for (;;) {
        printf("B");
        delay_ticks(50);  /* ~0.5s @100Hz */
    }
}
