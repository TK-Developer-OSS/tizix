// long 関数引数 / long 配列 r/w / long* 変数の *p と *p= / long を返すプロトタイプ
// EXPECT: YYYYYYY
// RUN: skip-x86  x86 backend の *p は 1バイト r/w のみ(既存制限)。z80 の long 経路が対象
#include <stdio.h>

unsigned long make_val(void);   /* 戻り値 long のプロトタイプ（本体は下） */

static void ck(int ok) { putchar(ok ? 'Y' : 'N'); }

static void show(unsigned long v) {      /* long を仮引数で受ける */
    ck(v == 300000UL);
}

static unsigned long g;
unsigned long make_val(void) { return g + 5UL; }

int main(void) {
    unsigned long arr[3];
    unsigned long *p;
    unsigned long x;
    unsigned long r;

    arr[0] = 100000UL;
    arr[1] = 300000UL;
    arr[2] = 99999UL;
    ck(arr[1] == 300000UL);              /* long 配列 読み */
    ck(arr[0] + arr[2] == 199999UL);     /* long 配列要素の算術 */
    arr[0] = arr[0] + 1UL;               /* long 配列 書き */
    ck(arr[0] == 100001UL);

    p = &x;
    *p = 123456UL;                       /* *long_p = long */
    ck(*p == 123456UL);                  /* *long_p 読み */
    ck(x == 123456UL);

    show(arr[1]);                        /* long 実引数 */

    g = 4000000000UL;                    /* > 2^31、上位ワードあり */
    r = make_val();                      /* プロトタイプ経由 long 戻り値 */
    ck(r == 4000000005UL);

    putchar('\n');
    return 0;
}
