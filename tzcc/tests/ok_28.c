#include <stdio.h>

/* && || の短絡評価 */
int main(int argc, char *argv[]) {
    int a = 3;
    int b = 0;

    if (a > 0 && a < 10) puts("A");     /* 真 && 真 -> A */
    if (a > 0 && b > 0)  puts("B");     /* 真 && 偽 -> 出ない */
    if (b > 0 || a > 0)  puts("C");     /* 偽 || 真 -> C */
    if (b > 0 || b < 0)  puts("D");     /* 偽 || 偽 -> 出ない */

    int r = a > 0 && a < 5;             /* 1 */
    putchar(48 + r);
    putchar(10);
    return 0;
}

// EXPECT: A
// EXPECT: C
// EXPECT: 1
