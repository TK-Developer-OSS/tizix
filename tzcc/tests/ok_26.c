#include <stdio.h>

/* 配列添字 a[i] の読み書き、*p デリファレンス、&x アドレス取得 */
int main(int argc, char *argv[]) {
    char buf[8];
    int i = 0;
    for (i = 0; i < 5; i = i + 1) {
        buf[i] = 65 + i;      /* 'A'..'E' 書き込み */
    }
    buf[5] = 10;              /* '\n' */
    buf[6] = 0;
    printf(buf);              /* ABCDE + 改行 */

    char c = buf[2];          /* 読み出し -> 'C' */
    putchar(c);
    putchar(10);

    char *p = buf;
    putchar(*p);              /* *p -> 'A' */
    putchar(10);
    return 0;
}

// EXPECT: ABCDE
// EXPECT: C
// EXPECT: A
