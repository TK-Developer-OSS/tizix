/* user/grep.c - 外部コマンド grep
 *   grep PATTERN FILE : ファイルからパターンを含む行を表示する。
 *   grep PATTERN       : 引数なしなら標準入力(パイプ後段 / < FILE)から検索。
 *
 *   #31 コストモデル対応:
 *     ・read_line の 3 仮引数を廃止しファイルスコープ変数を直接触る。tzcc は
 *       ローカルも全部 _DATA の静的領域に置くので、仮引数は「呼び出し側の
 *       push + 入口での退避」を余計に払うだけで得が無い。同時に、caller と
 *       callee が同名(from_stdin / fp)を持つ = **同一記憶域を共有する**
 *       という tzcc の罠も消える(tzcc 側にこの検出を入れた)。
 *     ・printf(546B)をやめ libtzc の prs(共有)で組む。行の出力も prs。
 */
#include "stdio.h"
#include "string.h"

#define LINE_LEN 128

static int   from_stdin;
static FILE *fp;
static char  line[LINE_LEN];

/* 1 行 line[] へ読む。戻り: 長さ / EOF かつ空なら -1。必ず NUL 終端。 */
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
    char *pattern;
    char *path;

    if (argc < 1) {
        puts("grep: usage: grep <pattern> [file]");
        return 1;
    }
    pattern = argv[0];
    path    = (argc >= 2 && argv[1] && argv[1][0]) ? argv[1] : 0;
    from_stdin = (path == 0);

    if (!from_stdin) {
        fp = fopen(path, "r");
        if (fp == NULL) {
            prs("grep: cannot open ");
            puts(path);
            return 1;
        }
    }

    while (read_line() >= 0) {
        if (strstr(line, pattern) != NULL)
            prs(line);
    }

    if (fp) fclose(fp);
    return 0;
}
