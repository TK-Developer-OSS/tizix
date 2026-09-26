// 32bit (long) サポート: 大きいリテラル / 32bit >= < == / 32bit -= と ++ /
//   long -> int 縮小キャスト / long + int / long ternary / { } 配列初期化子。
//   固定 epoch を UNIX 分解して日付文字列にする(date.c と同じアルゴリズム)。
// EXPECT: 2023-11-14 22:13:20
#include <stdio.h>

static const unsigned char dim_tbl[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
static const unsigned int pow10[] = { 10000, 1000, 100, 10, 1 };

static void pp(unsigned int n, unsigned char width) {
    unsigned char i;
    i = (unsigned char)(5 - width);
    while (i < 5) {
        unsigned int w = pow10[i];
        unsigned int d = 0;
        while (n >= w) { d++; n -= w; }
        putchar((char)('0' + d));
        i++;
    }
}

int main(void) {
    unsigned long t;
    unsigned long days;
    unsigned int y, m, dd, hh, mm, ss;
    unsigned char leap;
    unsigned int dm;

    t = 1700000000UL;

    days = 0;
    while (t >= 86400UL) { t -= 86400UL; days++; }
    hh = 0; while (t >= 3600UL) { t -= 3600UL; hh++; }
    mm = 0; while (t >= 60UL)   { t -= 60UL;   mm++; }
    ss = (unsigned int)t;

    y = 1970;
    for (;;) {
        unsigned long yd;
        leap = ((y & 3) == 0);
        yd = leap ? 366UL : 365UL;
        if (days < yd) break;
        days -= yd;
        y++;
    }

    m = 1; leap = ((y & 3) == 0);
    while (m <= 12) {
        dm = dim_tbl[m - 1];
        if (m == 2 && leap) dm = 29;
        if (days < (unsigned int)dm) break;
        days -= (unsigned int)dm;
        m++;
    }
    dd = (unsigned int)(days + 1);

    pp(y, 4); putchar('-'); pp(m, 2); putchar('-'); pp(dd, 2);
    putchar(' ');
    pp(hh, 2); putchar(':'); pp(mm, 2); putchar(':'); pp(ss, 2);
    putchar('\n');
    return 0;
}
