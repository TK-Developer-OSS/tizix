#include <stdio.h>

/* キャスト (type)expr : (char)/(unsigned char) は下位1バイト、他は透過 */
int main(int argc, char *argv[]) {
    int big;
    big = 321;                       /* 0x0141 */
    putchar(48 + (unsigned char)big - 65 + 5);  /* 下位=0x41=65 -> 48+65-65+5 = 53 '5' */

    int n;
    n = (int)'A';                    /* 65 透過 */
    putchar(n);                      /* 'A' */

    char *p;
    p = "Zx";
    putchar(*(char *)p);            /* 'Z' 透過 */
    putchar(10);
    return 0;
}

// EXPECT: 5AZ
