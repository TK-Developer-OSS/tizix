/* user/tee.c - 外部コマンド tee
 *   tee FILE : 標準入力の内容を画面とファイルに出力する。
 *
 *   掟: while ((c = getchar()) != EOF) と書いてはいけない。tzcc のパーサは
 *   条件式中の代入を式として扱えず、代入をループ外へ追い出したうえで比較を
 *   捨てる(cond が裸の c になる)。エラーにならず黙って誤コードが出るので、
 *   cat.c と同じ「for(;;) + 明示 break」形にする(#29 で tzcc 側もこの形を
 *   ビルドエラーにした)。
 *   #31: printf(546B)をやめ libtzc の prs(共有)で組む。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    char *path;
    FILE *fp;
    int c;

    if (argc < 1) {
        puts("tee: usage: tee <file>");
        return 1;
    }
    path = argv[0];

    fp = fopen(path, "w");
    if (fp == NULL) {
        prs("tee: cannot create ");
        puts(path);
        return 1;
    }

    for (;;) {
        c = getchar();
        if (c == EOF) break;               /* < FILE / パイプの終端 */
        if (c == 0x04) break;              /* ^D(端末直結時) */
        putchar(c);
        fputc(c, fp);
    }

    fclose(fp);
    return 0;
}
