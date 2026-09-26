// struct スカラーメンバのサイズ正しさ: char=1 / int=2 / long=4 バイトで r/w し、
// 隣のメンバを壊さないこと。p->m と v.m の両方。
// EXPECT: YYYYYYYY
#include <stdio.h>

struct Rec {
    char a;
    int b;
    char c;
    unsigned long d;
    int e;
};

static void ck(int ok) { putchar(ok ? 'Y' : 'N'); }

int main(void) {
    struct Rec r;
    struct Rec *p;

    r.a = 11;
    r.b = 3000;
    r.c = 22;
    r.d = 4000000000UL;
    r.e = 4444;

    p = &r;
    ck(r.a == 11);
    ck(r.b == 3000);              /* char a を書いても b が壊れない */
    ck(r.c == 22);
    ck(r.d == 4000000000UL);      /* long メンバ 4 バイト */
    ck(r.e == 4444);

    p->a = 99;                    /* ポインタ経由 char メンバ書き込み */
    ck(p->a == 99);
    ck(r.b == 3000);             /* b は不変 */

    r.d = r.d + 5UL;             /* long メンバ算術 */
    ck(r.d == 4000000005UL);

    putchar('\n');
    return 0;
}
