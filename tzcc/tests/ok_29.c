#include <stdio.h>

#define NL     10
#define COUNT  5
#define GREET  "Hi\n"
#define BASE   ('A')
#define LIMIT  (COUNT + 2)   /* マクロがマクロを参照 */

int main(int argc, char *argv[]) {
    int i = 0;
    for (i = 0; i < COUNT; i++) {
        putchar(BASE + i);          /* ABCDE */
    }
    putchar(NL);

    for (i = 0; i < LIMIT; i++) {
        putchar(48 + i);            /* 0123456 (=7個) */
    }
    putchar(NL);

    printf(GREET);                  /* Hi */
    return 0;
}

// EXPECT: ABCDE
// EXPECT: 0123456
// EXPECT: Hi
