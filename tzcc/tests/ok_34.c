#include <stdio.h>

/* 乗除算の実値確認（従来テストは結果を印字していなかった） */
int main(int argc, char *argv[]) {
    int a;
    int b;
    a = 6;
    b = 7;
    putchar(48 + a * b - 42 + 3);   /* 42-42+3 = 3 */
    putchar(48 + 100 / 20);         /* 5 */
    putchar(48 + 255 / 100);        /* 2 */
    putchar(48 + 3 * 3 * 3 - 25);   /* 27-25 = 2 */
    putchar(48 + 1000 / 100);       /* 10 -> ':' (58) */
    putchar(10);
    return 0;
}

// EXPECT: 3522:
