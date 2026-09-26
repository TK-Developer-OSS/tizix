#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("A=%c\n", 65);
    printf("B=%d,%d\n", 10, 20);
    printf("C=%x\n", 255);
    printf("D=%s\n", "str");
    puts("end");
    return 0;
}

// RUN: skip Tizix 実機専用
