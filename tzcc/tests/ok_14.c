#include <stdio.h>

int main(int argc, char *argv[]) {
    FILE *f1 = stdout;
    FILE *f2 = stderr;

    fputs("Hello stdout!\n", f1);
    fputs("Error stderr!\n", f2);

    char c = fgetc(stdin);
    fputc(c, f1);

    return 0;
}

// RUN: skip needs-stdin
