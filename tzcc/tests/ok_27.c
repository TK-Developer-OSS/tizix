#include <stdio.h>

/* ++ -- と複合代入 += -= */
int main(int argc, char *argv[]) {
    int i = 0;
    for (i = 0; i < 5; i++) {
        putchar(48 + i);     /* 01234 */
    }
    putchar(10);

    int j = 10;
    j -= 3;                  /* 7 */
    j += 1;                  /* 8 */
    putchar(48 + j);
    putchar(10);

    int k = 3;
    --k;                     /* 2 */
    k--;                     /* 1 */
    putchar(48 + k);
    putchar(10);
    return 0;
}

// EXPECT: 01234
// EXPECT: 8
// EXPECT: 1
