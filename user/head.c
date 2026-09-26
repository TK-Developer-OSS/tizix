/* user/head.c - 外部コマンド head
 *   head [-n N] [-N] [FILE] : ファイル(省略時は標準入力)の先頭 N 行を表示。
 *                             N の既定は 10。
 *   -N        : 数字だけの短縮形 (head -5 file)
 *   -n N      : GNU 互換
 *   引数なし / FILE 省略 : stdin(パイプ後段 / < FILE / 端末)
 *
 *   #26: tzcc ビルド(iy_reg 卒業)。argv[] は progname 無し・argv[0] が最初の引数。
 *   sh は is_path_cmd で相対パス引数を絶対化するが、"-n" の次(行数)は
 *   build_pack が素通しする(sh.c prev_optarg)。
 */
#include "stdio.h"

static int str_to_int(const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *path = 0;
    int limit = 10;
    int i = 0;
    int lines = 0;
    int c;
    FILE *fp;

    while (i < argc) {
        char *a = argv[i];
        if (a == 0 || a[0] == 0) { i++; continue; }
        if (a[0] != '-') { path = a; break; }        /* 最初の非オプション = FILE */
        if (a[1] == 'n' && a[2] == 0) {              /* -n N */
            i++;
            if (i < argc) limit = str_to_int(argv[i]);
            i++;
            continue;
        }
        if (a[1] >= '0' && a[1] <= '9') {            /* -N */
            limit = str_to_int(&a[1]);
            i++;
            continue;
        }
        i++;                                        /* 未知オプションは黙って無視 */
    }

    if (path == 0) {
        while (lines < limit) {
            c = getchar();
            if (c == EOF || c == 0x04) break;
            putchar(c);
            if (c == '\n') lines++;
        }
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        puts("head: cannot open");
        return 1;
    }
    while (lines < limit) {
        c = fgetc(fp);
        if (c == EOF) break;
        putchar(c);
        if (c == '\n') lines++;
    }
    fclose(fp);
    return 0;
}
