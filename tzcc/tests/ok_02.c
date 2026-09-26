#include <stdio.h>

int main(int argc, char *argv[]) {
    char c;
    c = 'A';
    putc(c);
    return 0;
}
// EXPECT: A
