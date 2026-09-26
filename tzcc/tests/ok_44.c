// 0 との大小比較を符号付きの意図として扱う（EOF センチネル `while (n >= 0)` 等）。
// tzcc は符号を追わないが x<0 / x>=0 / x>0 / x<=0 は h の bit7 で判定する。
// EXPECT: YYYYYYYYYYYY
#include <stdio.h>

static void ck(int b) { putchar(b ? 'Y' : 'N'); }

int g_left;                 /* 疑似 read_line の残り回数（static-local を使わない） */

static int rl(void) {       /* g_left 回 正の値を返し、その後 -1(EOF 相当) */
    if (g_left <= 0) return -1;
    g_left = g_left - 1;
    return 42;
}

int main(void) {
    int a;
    int neg;
    int zero;
    int cnt;

    neg = 0 - 5;
    zero = 0;

    ck(neg < 0);        ck(!(neg >= 0));
    ck(!(neg > 0));     ck(neg <= 0);
    ck(zero >= 0);      ck(!(zero < 0));
    ck(!(zero > 0));    ck(zero <= 0);
    a = 7;
    ck(a > 0);          ck(0 < a);          /* x <op> 0 と 0 <op> x の両形 */

    g_left = 3;
    cnt = 0;
    while (rl() >= 0) { cnt = cnt + 1; if (cnt > 100) break; }
    ck(cnt == 3);
    ck(a >= 0);

    putchar('\n');
    return 0;
}
