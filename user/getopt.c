/* user/getopt.c - 再入可能 getopt(状態は呼び出し側 getopt_t)。
 *   strchr は string.rel(手書き PIC)から引く。printf は drv_tbl[2]。
 *   掟: 大小比較は unsigned、除算乗算なし。
 */
#include "getopt.h"
#include "stdio.h"
#include "string.h"

void opt_init(getopt_t *g)
{
    g->optind = 0;              /* argv[0] からが引数(progname 無し) */
    g->optpos = 1;              /* '-' の次から */
    g->optopt = 0;
    g->opterr = 1;
    g->optarg = 0;
}

int getopt_r(getopt_t *g, const char *prog, int argc, char **argv,
             const char *optstring)
{
    unsigned ac = (unsigned)argc;
    char *arg;
    char *cp;
    int   c;

    g->optarg = 0;

    if (g->optind >= ac)
        return -1;

    arg = argv[g->optind];
    if (arg == 0 || arg[0] != '-' || arg[1] == 0)
        return -1;                              /* 非オプション / "-" 単体 */

    if (arg[1] == '-' && arg[2] == 0) {         /* "--" 明示終端 */
        g->optind++;
        return -1;
    }

    c = (int)(unsigned char)arg[g->optpos];
    g->optopt = c;
    cp = strchr(optstring, c);

    if (cp == 0 || c == ':') {                  /* 未知オプション */
        if (g->opterr && optstring[0] != ':')
            printf("%s: illegal option -- %c\n", prog, c);
        g->optpos++;
        if (arg[g->optpos] == 0) { g->optind++; g->optpos = 1; }
        return '?';
    }

    if (cp[1] == ':') {                         /* 引数を取るオプション */
        if (arg[g->optpos + 1] != 0) {          /* -oVALUE 形式 */
            g->optarg = &arg[g->optpos + 1];
            g->optind++;
        } else {                               /* -o VALUE 形式 */
            g->optind++;
            if (g->optind >= ac) {
                g->optpos = 1;
                if (g->opterr && optstring[0] != ':')
                    printf("%s: option requires an argument -- %c\n", prog, c);
                return (optstring[0] == ':') ? ':' : '?';
            }
            g->optarg = argv[g->optind];
            g->optind++;
        }
        g->optpos = 1;
    } else {                                   /* 単純フラグ(連結対応) */
        g->optpos++;
        if (arg[g->optpos] == 0) { g->optind++; g->optpos = 1; }
    }
    return c;
}
