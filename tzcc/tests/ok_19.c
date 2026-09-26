#include <stdio.h>

/* if / else if / else の分岐 */
int main(int argc, char *argv[]) {
    int n = 2;

    if (n == 1) {
        puts("one");
    } else if (n == 2) {
        puts("two");
    } else {
        puts("many");
    }

    if (n) puts("nonzero");
    if (!n) puts("zero");

    return 0;
}

// EXPECT: two
// EXPECT: nonzero
