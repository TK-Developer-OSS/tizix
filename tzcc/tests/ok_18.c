#include <stdio.h>

/* 比較演算子 == != < > <= >= と 単項 ! - */
int main(int argc, char *argv[]) {
    int a = 3;
    int b = 5;
    int r = 0;

    r = a < b;      /* 1 */
    r = a > b;      /* 0 */
    r = a <= 3;     /* 1 */
    r = b >= 6;     /* 0 */
    r = a == 3;     /* 1 */
    r = a != b;     /* 1 */
    r = !r;         /* 0 */
    r = -a;         /* -3 */
    r = a + b * 2;  /* 13 (優先順位) */

    if (a < b) {
        printf("less\n");
    } else {
        printf("not less\n");
    }
    return 0;
}

// EXPECT: less
