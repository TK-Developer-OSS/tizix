/* user/sed.c - 外部コマンド sed(簡易版)
 *   sed s/OLD/NEW/[g] FILE : ファイルの各行を置換して表示する。
 *   sed s/OLD/NEW/[g]      : 引数なしなら標準入力(パイプ後段 / < FILE)から。
 *   末尾の g を付けると行内の全一致を置換する(既定は行ごとに最初の 1 個)。
 *
 *   #34。ビルドは tzcc(--tizix-user)。iy_reg 経由の SDCC 版は分岐の多い
 *   引数解析で壊れる実績があるため、他の coreutils と同じ経路に揃えてある。
 *
 *   コストモデル(#31)対応:
 *     ・仮引数を持たない。tzcc はローカルも _DATA の静的領域に置くので、
 *       仮引数は「呼び出し側の push + 入口での退避」を余計に払うだけ。
 *     ・パターンは argv[0] を破壊的に NUL で切ってそこを指すだけにする。
 *       old_pat[]/new_pat[] の実体配列と strcpy を持たない。
 *     ・printf は使わない(546B の 1 モジュール)。prs / puts で組む。
 */
#include "stdio.h"
#include "string.h"

#define LINE_LEN 128

static int   from_stdin;
static FILE *fp;
static char  line[LINE_LEN];

static char *spec;          /* argv[0] = "s/OLD/NEW/[g]" */
static char *old_pat;
static char *new_pat;
static int   old_len;
static int   glob_all;      /* 末尾 g */

/* 1 行 line[] へ読む。戻り: 長さ / EOF かつ空なら -1。必ず NUL 終端。
 * grep.c / uniq.c と同一の実装(改行はそのまま残す)。 */
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

/* spec を破壊的に切って old_pat / new_pat / old_len / glob_all を作る。
 * 戻り: 1=OK / 0=形式不正。 */
static int parse_spec(void)
{
    char *pp;
    char *pe;

    pp = spec;
    if (pp[0] != 's') return 0;
    if (pp[1] != '/') return 0;

    pp = pp + 2;
    pe = strchr(pp, '/');
    if (pe == NULL) return 0;
    *pe = 0;
    old_pat = pp;
    old_len = strlen(pp);
    if (old_len == 0) return 0;     /* 空パターンは一致が進まないので拒否 */

    pp = pe + 1;
    pe = strchr(pp, '/');
    if (pe == NULL) return 0;
    *pe = 0;
    new_pat = pp;

    if (pe[1] == 'g') glob_all = 1;
    return 1;
}

int main(int argc, char **argv)
{
    char *path;
    char *cur;
    char *hit;

    if (argc < 1) {
        puts("sed: usage: sed s/old/new/[g] [file]");
        return 1;
    }
    spec = argv[0];
    if (parse_spec() == 0) {
        puts("sed: usage: sed s/old/new/[g] [file]");
        return 1;
    }

    path = (argc >= 2 && argv[1] && argv[1][0]) ? argv[1] : 0;
    from_stdin = (path == 0);

    if (!from_stdin) {
        fp = fopen(path, "r");
        if (fp == NULL) {
            prs("sed: cannot open ");
            puts(path);
            return 1;
        }
    }

    while (read_line() >= 0) {
        cur = line;
        for (;;) {
            hit = strstr(cur, old_pat);
            if (hit == NULL) break;
            *hit = 0;               /* 一致の手前で切って前半を出す */
            prs(cur);
            prs(new_pat);
            cur = hit + old_len;
            if (glob_all == 0) break;
        }
        prs(cur);
    }

    if (fp) fclose(fp);
    return 0;
}
