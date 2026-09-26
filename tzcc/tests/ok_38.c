#include <stdio.h>

/* 配列メンバへの添字  p->buf[i]  と  s.buf[i] */
typedef struct S {
    int id;
    char buf[8];
} S;

S obj;

void fill(S *p) {
    int i;
    for (i = 0; i < 4; i++) {
        p->buf[i] = 65 + i;    /* p->m[i] 書き込み */
    }
    p->buf[4] = 0;
    p->id = 7;
}

int main(int argc, char *argv[]) {
    S *p;
    p = &obj;
    fill(p);
    int i;
    for (i = 0; i < 4; i++) {
        putchar(p->buf[i]);    /* p->m[i] 読み  -> ABCD */
    }
    putchar(48 + p->id);       /* 7 */
    putchar(10);

    putchar(obj.buf[1]);       /* s.m[i]  -> B */
    putchar(10);
    return 0;
}

// EXPECT: ABCD7
// EXPECT: B
