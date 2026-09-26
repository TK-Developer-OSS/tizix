#include <stdio.h>

/* switch / case / default / break / continue / ?: */
int main(int argc, char *argv[]) {
    int i;
    for (i = 0; i < 6; i++) {
        if (i == 2) continue;      /* 2 は飛ばす */
        if (i == 5) break;         /* 5 で終了 */
        putchar(48 + i);           /* 0 1 3 4 */
    }
    putchar(10);

    for (i = 0; i < 4; i++) {
        switch (i) {
            case 0:
                putchar(65);       /* A */
                break;
            case 1:
                putchar(66);       /* B */
                break;
            case 2:
                putchar(67);       /* C（fallthrough で D も）*/
            case 3:
                putchar(68);       /* D */
                break;
            default:
                putchar(90);
        }
    }
    putchar(10);

    int x;
    x = 7;
    putchar(x > 5 ? 89 : 78);      /* 'Y' */
    putchar(x < 5 ? 89 : 78);      /* 'N' */
    putchar(10);
    return 0;
}

// EXPECT: 0134
// EXPECT: ABCDD
// EXPECT: YN
