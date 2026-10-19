/* user/wc.c - 外部コマンド wc
 *   wc [-l] [-w] [-c] [FILE...] : 各ファイルの行数・単語数・文字数を数える。
 *                  複数指定すると最後に total 行を出す。
 *                  -l / -w / -c(-lw 等の連結可)で出す欄を選ぶ。無指定なら 3 欄。
 *   wc [-l ...]  : ファイル指定が無ければ標準入力(パイプ後段 / < FILE)を集計。
 *
 *   #26: tzcc ビルド(iy_reg 卒業)。argv[] は progname 無し・argv[0] が最初の引数。
 *   掟: カウンタは unsigned 16bit(long を使うと %ld/32bit 演算で 1 ブロックを
 *   超えてパイプが不可になる)。tizix のファイルは 64KB 未満なので十分。
 *
 *   #31 コストモデル対応:
 *     ・count_stream の 5 引数(うち 3 個が出力先ポインタ)を廃止。tzcc は
 *       ローカルも全部 _DATA の静的領域に置くので、ファイルスコープ変数へ
 *       直接書けば「仮引数の入口退避」「&var のアドレス取得」「*p=v の間接
 *       ストア」が丸ごと消える。単独で -304B。
 *     ・printf(546B)をやめ libtzc の prnum/prs(共有)で組む。
 *   #81: -l / -w / -c を受ける。以前はオプションを解析せず、`wc -l FILE` で
 *     "-l" をファイル名として開こうとして "wc: cannot open -l" を出していた。
 */
#include "stdio.h"

static int from_stdin;
static FILE *fp;
static unsigned lines, words, chars;
static int opt_l, opt_w, opt_c;            /* 全部 0 なら 3 欄とも出す */

static void count_stream(void)
{
    int in_word = 0;
    int c;

    lines = 0; words = 0; chars = 0;
    for (;;) {
        c = from_stdin ? getchar() : fgetc(fp);
        if (c == EOF || (from_stdin && c == 0x04)) break;
        chars++;
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
}

/* 選ばれた欄を空白区切りで出す(末尾に改行は付けない)。 */
static void put3(unsigned n1, unsigned n2, unsigned n3)
{
    int sep = 0;

    if (opt_l) { prnum(n1); sep = 1; }
    if (opt_w) { if (sep) putchar(' '); prnum(n2); sep = 1; }
    if (opt_c) { if (sep) putchar(' '); prnum(n3); }
}

int main(int argc, char **argv)
{
    unsigned tl = 0, tw = 0, tc = 0;
    int nfile = 0;
    int nargs = 0;
    int rc = 0;
    int i, j;

    for (i = 0; i < argc; i++) {
        char *a = argv[i];
        if (a == 0 || a[0] == 0) continue;
        if (a[0] == '-' && a[1] != 0) {
            for (j = 1; a[j]; j++) {
                if (a[j] == 'l')      opt_l = 1;
                else if (a[j] == 'w') opt_w = 1;
                else if (a[j] == 'c') opt_c = 1;
                else { puts("usage: wc [-l] [-w] [-c] [FILE...]"); return 1; }
            }
            continue;
        }
        nargs++;
    }
    if (!opt_l && !opt_w && !opt_c) { opt_l = 1; opt_w = 1; opt_c = 1; }

    if (nargs == 0) {                         /* stdin */
        from_stdin = 1;
        count_stream();
        put3(lines, words, chars);
        putchar(10);
        return 0;
    }

    from_stdin = 0;
    for (i = 0; i < argc; i++) {
        char *path = argv[i];
        if (path == 0 || path[0] == 0) continue;
        if (path[0] == '-' && path[1] != 0) continue;   /* オプション */

        fp = fopen(path, "r");
        if (fp == NULL) {
            prs("wc: cannot open ");
            puts(path);
            rc = 1;
            continue;
        }
        count_stream();
        fclose(fp);
        put3(lines, words, chars);
        putchar(' ');
        puts(path);
        tl = tl + lines; tw = tw + words; tc = tc + chars;
        nfile++;
    }

    if (nfile > 1) {
        put3(tl, tw, tc);
        puts(" total");
    }

    return rc;
}
