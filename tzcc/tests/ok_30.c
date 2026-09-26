#include <stdio.h>

/* ビット演算 & | ^ ~ << >> */
int main(int argc, char *argv[]) {
    int a = 12;      /* 1100 */
    int b = 10;      /* 1010 */

    putchar(48 + (a & b));    /* 1000 = 8  */
    putchar(48 + (a | b));    /* 1110 = 14 -> '>' (62) いや 48+14=62='>' */
    putchar(48 + (a ^ b));    /* 0110 = 6  */
    putchar(10);

    putchar(48 + (1 << 3));   /* 8 */
    putchar(48 + (64 >> 4));  /* 4 */
    putchar(10);

    int c = 0;
    c = ~a;                   /* ~12 = -13 (0xFFF3) */
    c = c & 15;               /* 下位4bit = 3 */
    putchar(48 + c);
    putchar(10);
    return 0;
}

// EXPECT: 8>6
// EXPECT: 84
// EXPECT: 3
