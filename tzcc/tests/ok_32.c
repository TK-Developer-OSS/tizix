#include <stdio.h>

typedef struct Point {
    int x;
    int y;
    int tag;
    struct Point *next;
} Point;

int main(int argc, char *argv[]) {
    Point pt;
    Point *p;

    p = &pt;
    p->x = 3;
    p->y = 4;
    p->tag = 12;
    p->next = 0;

    putchar(48 + pt.x);            /* '.' 読み  -> '3' */
    putchar(48 + p->y);            /* '->' 読み -> '4' */
    if (p->tag == 12 && p->next == 0) putchar(89);   /* 'Y' */
    putchar(10);

    if (sizeof(Point) == 4 * sizeof(int)) putchar(89); else putchar(78);  /* Y */
    putchar(10);
    return 0;
}

// EXPECT: 34Y
// EXPECT: Y
