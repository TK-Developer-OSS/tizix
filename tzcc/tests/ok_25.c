#include <stdio.h>

/* printf 書式: %d %i %u %x %c %s %% と、幅指定の読み飛ばし */
int main(int argc, char *argv[]) {
    printf("d=%d u=%u x=%x c=%c s=%s %%\n", 300, 40000, 300, 65, "abc");
    printf("neg=%d zero=%d\n", -5, 0);
    printf("plain [%d]\n", 42);
    return 0;
}

// EXPECT: d=300 u=40000 x=12c c=A s=abc %
// EXPECT: neg=-5 zero=0
// EXPECT: plain [42]
