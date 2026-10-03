/* user/du.c - 外部コマンド du (簡易版)
 *   du        : カレントディレクトリのファイルサイズ合計。
 *   du DIR    : そのディレクトリの合計。
 *   du FILE   : そのファイルのサイズ(opendir が失敗したらファイルとして扱う)。
 *
 *   掟: while ((t = readdir(name)) != 0) と書いてはいけない。tzcc のパーサは
 *   条件式中の代入を式として扱えず、代入をループ外へ追い出す(cond が裸の t に
 *   なり、本体はループの後ろへ落ちる)。#29 で tzcc 側もこの形をビルドエラーに
 *   したので、for(;;) + 明示 break で書く。
 *   #31: printf(546B)をやめ libtzc の prnuml/prs(共有)で組む。合計は 64KB を
 *   超えうるので 32bit のまま prnuml で出す。
 */
#include "stdio.h"
#ifndef TZ_NAME_MAX
#define TZ_NAME_MAX 16   /* z80: 8.3(gcc 側は stdio.h が長いファイル名の 65 にする。#114) */
#endif
#include "string.h"

int main(int argc, char **argv)
{
    char *path;
    char name[TZ_NAME_MAX];
    FILE *fp;
    int t;
    unsigned long total = 0;

    path = (argc >= 1 && argv[0] && argv[0][0]) ? argv[0] : ".";

    if (opendir(path) != 0) {
        /* ディレクトリでない → ファイルとして開き、末尾シークでサイズを得る。
         * 巻き戻しはしないので FF_FS_TINY 窓を壊さない。 */
        fp = fopen(path, "r");
        if (fp == NULL) {
            prs("du: cannot open ");
            puts(path);
            return 1;
        }
        fseek(fp, 0L, SEEK_END);
        total = (unsigned long)ftell(fp);
        fclose(fp);
        prnuml(total);
        putchar(' ');
        puts(path);
        return 0;
    }

    for (;;) {
        t = readdir(name);
        if (t == 0) break;
        if (t == 1) total += readdir_size();   /* ファイルのみ集計 */
    }
    closedir();

    prnuml(total);
    putchar(' ');
    puts(path);
    return 0;
}
