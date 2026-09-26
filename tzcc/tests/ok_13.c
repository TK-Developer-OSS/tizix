#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Test to read one character from a file
    char *f = fopen("test.txt", "r");
    int c = fgetc(f);
    putc(c);
    fclose(f);

    // Output a newline
    puts("\n");
    return 0;
}

// RUN: skip file-io(BDOS)
