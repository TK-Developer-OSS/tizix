#include <stdio.h>

/* while ループ: 'A'..'E' を出力 */
int main(int argc, char *argv[]) {
    int c = 65;
    while (c <= 69) {
        putchar(c);
        c = c + 1;
    }
    putchar(10);
    return 0;
}

// EXPECT: ABCDE
