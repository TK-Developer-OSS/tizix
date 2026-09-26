#include <stdio.h>

typedef struct N { int v; struct N *nx; } N;

int main(int argc, char *argv[]) {
    unsigned int x;
    long y;
    x = 200;
    y = 300;
    putchar(48 + (x + y) / 100);     /* 500/100 = 5 */

    N a;
    N b;
    N *pa;
    N *pb;
    pa = &a;
    pb = &b;
    pa->v = 7;
    pb->v = 9;
    pa->nx = pb;
    putchar(48 + pa->v);             /* 7 */
    putchar(48 + pa->nx->v);         /* 連鎖: 9 */
    pa->nx->v = 4;                   /* 連鎖代入 */
    putchar(48 + pb->v);             /* 4 */
    putchar(10);
    return 0;
}

// EXPECT: 5794
