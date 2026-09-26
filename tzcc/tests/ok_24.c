#include <stdio.h>

/*
 * TEST.TXT を開いて全行を表示する。
 *
 * 元の版のバグ:
 *   1. fgets(f, str, 32)  … 引数順が違う。正しくは fgets(str, size, f)。
 *      crt0 の _fgets は 2(ix)=buf / 4(ix)=size / 6(ix)=FILE* を期待するので、
 *      f を buf として、32 を FILE* として扱い、でたらめな FCB で BDOS を叩いて
 *      "Bdos Err On D: Bad Sector" になっていた。
 *   2. while(feof(f))     … 判定が逆。EOF になるまで回すなら !feof(f)。
 *      ここでは fgets の戻り値 (EOF で NULL) でループする形にした。
 *   3. fopen の NULL チェックが無い。
 */
int main(int argc, char *argv[]) {
    char str[64];
    FILE *f;

    f = fopen("TEST.TXT", "r");
    if (f == 0) {
        printf("cannot open TEST.TXT\n");
        return 1;
    }

    while (fgets(str, 64, f) != 0) {
        fputs(str, stdout);
    }

    fclose(f);
    return 0;
}

// RUN: skip file-io(BDOS)
