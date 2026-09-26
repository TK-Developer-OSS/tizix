#include "stdio.h"

int main(int argc, char **argv)
{
    printf("cp_dbg: start\n");
    if (argc < 1 || !argv[0]) {
        printf("cp_dbg: no args\n");
        return 1;
    }
    printf("cp_dbg: raw arg: '%s'\n", argv[0]);

    FILE *fp = fopen("HELLO.BIN", "r");
    if (!fp) {
        printf("cp_dbg: cannot open HELLO.BIN\n");
        return 1;
    }
    printf("cp_dbg: HELLO.BIN opened successfully\n");
    fclose(fp);
    return 0;
}
