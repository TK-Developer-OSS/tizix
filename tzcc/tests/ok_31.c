#include <stdio.h>

enum { RED, GREEN, BLUE };            /* 0 1 2 */
enum Sz { SMALL = 10, MEDIUM, LARGE };/* 10 11 12 */

int main(int argc, char *argv[]) {
    int c = GREEN;                    /* 1 */
    putchar(48 + c);

    int s = MEDIUM;                   /* 11 */
    putchar(48 + s - 10);            /* '1' */

    if (BLUE == 2 && LARGE == 12) putchar(89);  /* 'Y' */
    putchar(10);
    return 0;
}

// EXPECT: 11Y
