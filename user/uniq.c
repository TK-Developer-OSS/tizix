/* user/uniq.c - 外部コマンド uniq
 *   uniq FILE : 隣接する重複行を削って表示する。
 *   uniq      : 引数なしなら標準入力(パイプ後段 / < FILE)を処理。
 *
 *   スタック注意: 1 ブロック(4096B)プロセスの実効スタックは
 *   3776 - バイナリサイズしかなく、line[]/prev[] は _DATA の末尾 = SP の真下に
 *   置かれる。#29 の時点では uniq.bin=3640B でスタックが 136B しか残らず、
 *   fgetc から FatFs へ降りるとスタックが両バッファを踏み潰して `uniq FILE` が
 *   ゴミを出していた(`cat f | uniq` は getchar 経路が浅いので無事だった)。
 *   #30 の jr 化、#31 の IX 相対化 / printf 外し / 仮引数削減でサイズが下がり、
 *   LINE_LEN 128 のままで十分な余裕がある。
 *
 *   #31 コストモデル対応: read_line の 3 仮引数を廃止(grep.c と同じ理由)。
 */
#include "stdio.h"
#include "string.h"

#define LINE_LEN 128

static int   from_stdin;
static FILE *fp;
static char  line[LINE_LEN];
static char  prev[LINE_LEN];

static int read_line(void)
{
    int rli = 0;
    int rlc;
    for (;;) {
        rlc = from_stdin ? getchar() : fgetc(fp);
        if (rlc == EOF || (from_stdin && rlc == 0x04)) {
            if (rli == 0) return -1;
            line[rli] = 0;
            return rli;
        }
        if (rli < LINE_LEN - 1) line[rli++] = (char)rlc;
        if (rlc == '\n') { line[rli] = 0; return rli; }
    }
}

int main(int argc, char **argv)
{
    char *path;
    int first = 1;

    (void)argc;
    path = (argv && argv[0] && argv[0][0]) ? argv[0] : 0;
    from_stdin = (path == 0);

    if (!from_stdin) {
        fp = fopen(path, "r");
        if (fp == NULL) {
            prs("uniq: cannot open ");
            puts(path);
            return 1;
        }
    }

    prev[0] = 0;
    while (read_line() >= 0) {
        if (first || strcmp(line, prev) != 0) {
            prs(line);
            strcpy(prev, line);
            first = 0;
        }
    }

    if (fp) fclose(fp);
    return 0;
}
