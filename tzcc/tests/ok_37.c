#include <stdio.h>

/* 構造体配列 + arr[i].m / arr[i]->m */
typedef struct Rec { int a; int b; struct Rec *link; } Rec;

Rec pool[4];

int main(int argc, char *argv[]) {
    int i;
    for (i = 0; i < 4; i++) {
        pool[i].a = 65 + i;      /* A B C D */
        pool[i].b = i;
    }
    pool[0].link = &pool[3];

    for (i = 0; i < 4; i++) {
        putchar(pool[i].a);      /* ABCD */
    }
    putchar(10);

    putchar(pool[0].link->a);    /* pool[3].a = 'D' : arr[i]->m 連鎖 */
    putchar(48 + pool[2].b);     /* 2 */
    putchar(10);
    return 0;
}

// EXPECT: ABCD
// EXPECT: D2
