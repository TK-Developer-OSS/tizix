#include <stdio.h>

int main(int argc, char *argv[]) {
    char *f = fopen("test.txt", "w");
    fputs("File IO Test\n", f);
    fclose(f);
    return 0;
}

// RUN: skip file-io(BDOS)
