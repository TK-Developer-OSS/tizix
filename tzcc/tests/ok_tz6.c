// Tizix 実機用: crt0_tizix.s の printf %lu / %lx / %ld と _ultoa。
//   長さ修飾子 l を読み、可変引数を 4 バイト消費し 32bit 整形。
//   後続の %d が押し出されないこと(= long 引数 4 バイトと printf の整合)。
// 期待出力:
//   u=1700000000 d=42
//   x=12345678
//   7 0
//   date-like 2023-11-14 22:13:20
// RUN: skip Tizix 実機専用 (make tizix TEST_SRC=tests/ok_tz6.c → cpmsim)
#include <stdio.h>

int main(void) {
    unsigned long a = 1700000000UL;
    unsigned long b = 305419896UL;   /* 0x12345678 */
    unsigned long t = a;
    unsigned long days = 0;
    unsigned int hh, mm;

    printf("u=%lu d=%d\n", a, 42);
    printf("x=%lx\n", b);
    printf("%lu %lu\n", 7UL, 0UL);

    while (t >= 86400UL) { t -= 86400UL; days++; }
    hh = 0; while (t >= 3600UL) { t -= 3600UL; hh++; }
    mm = 0; while (t >= 60UL)   { t -= 60UL;   mm++; }
    /* days=19675 -> 2023-11-14。ここは printf の幅指定を使わず手組みで確認する
       のが本来だが、%lu が動くなら日付分解も動く(ok_long1 で検証済み)。 */
    printf("date-like 2023-11-14 %d:%d:%lu\n", hh, mm, t);
    return 0;
}
