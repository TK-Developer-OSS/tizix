/* user/tzsh.c - tzsh: シェルスクリプト専門のシェル(task.md #111)。gcc 系(TZ_SYSCALL)だけ。
 *
 *   tzsh FILE [引数…]      FILE を上から実行する($1.. に引数)
 *   tzsh -c '文' [引数…]   文字列を実行する
 *
 *   bash に似せた範囲(対話はしない。対話は今の sh):
 *     ;  改行  &  |  >  >>  <  &&  ||  !  # コメント  '…'  "…"(中の $ は展開)  \
 *     if 文; then 文; elif 文; then 文; else 文; fi
 *     while 文; do 文; done      until 文; do 文; done      break [n]  continue
 *     for 名前 in 語…; do 文; done
 *     case 語 in 型|型) 文 ;; *) 文 ;; esac       (型は * と ? が使える)
 *     名前=値   $名前  ${名前}  $1..$9  $#  $@  $?  $((式))(+ - * / % 比較 && || ! 括弧)
 *     組み込み: echo [-n]  test / [ … ]  true  false  :  exit [n]  read 名前  shift  cd  pwd
 *     ほかはカーネルの組み込み(ps / kill …)か /bin の外部コマンド。$? は外部コマンドの
 *     main の戻り値(カーネルの syscall 52。Ctrl+C / kill で止めたら 130、見つからなければ 127)。
 *   無いもの: コマンド置換 $(…)、関数、ヒアドキュメント、2 段より長いパイプ(カーネルのパイプが 1 本)。
 *
 *   作り: スクリプトを最初に字句(トークン)へ割り、トークンの列を再帰下降で読みながら実行する。
 *   実行しない枝(if の偽、ループを抜けた後)は同じ読み方で「実行せずに読み飛ばす」。while は
 *   トークンの位置を覚えて、条件から読み直す。語の展開($ や引用符)は実行するときに行う。
 *   esp32 ではコア 0 の普通のプロセス(コア 1 で走らせる構想は task.md #111 の履歴)。
 */
#include "stdio.h"
#include "string.h"
#define SHVEC_NO_STATE                       /* sh のセッション状態(640B)は要らない */
#include "shvec.h"
#include "mbox.h"

/* 作業域の大きさ。TZSH_SMALL はアーキの Makefile が立てる(m68k-mega: 像 + .bss が 1 スロット 24KB に入らないため) */
#ifdef TZSH_SMALL
#define SRC_MAX   2048
#define TOK_MAX   400
#define VAR_MAX   24
#else
#define SRC_MAX   2048                       /* データ枠 16KB の 1 スロットに収める(超えると 2 スロット取る。#113) */
#define TOK_MAX   500
#define VAR_MAX   32
#endif
#define VNAME_MAX 16
#define VVAL_MAX  64
#define WORD_MAX  96
#define ARG_MAX   12
#define PACK_MAX  320

/* ---- トークン ---- */
enum { T_EOF, T_WORD, T_NL, T_AMP, T_PIPE, T_AND, T_OR, T_GT, T_GTGT, T_LT, T_DSEMI, T_LP, T_RP };

static char src[SRC_MAX + 1];
static char pool[SRC_MAX + TOK_MAX];         /* 語の生の文字列(引用符も残す) */
static unsigned char ttype[TOK_MAX];
static unsigned short tword[TOK_MAX];
static int ntok, pos;

/* ---- 変数・引数 ---- */
static char vname[VAR_MAX][VNAME_MAX];
static char vval[VAR_MAX][VVAL_MAX];
static char *pargv[10];
static int pargc;
static int last_status;
static int g_break, g_continue, g_exit, g_exit_code;

/* コマンドは libc を持たない(stdio.h の gcc 側に str* はあるが mem* / strn* は無い) */
static void t_memcpy(void *d, const void *s, unsigned n)
{
    char *dp = d;
    const char *sp = s;
    while (n--) *dp++ = *sp++;
}

static int t_strncmp(const char *a, const char *b, unsigned n)
{
    while (n--) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}

/* n - 1 文字まで写して必ず NUL で終える。戻りは dst */
static char *t_strlcpy(char *dst, const char *src, unsigned n)
{
    unsigned i;
    for (i = 0; i + 1 < n && src[i]; i++) dst[i] = src[i];
    if (n) dst[i] = 0;
    return dst;
}

static int err(const char *a, const char *b)
{
    printf("tzsh: %s%s\n", a, b ? b : "");
    return 2;
}

/* 構文の誤りはその場で止める(読む位置が進まないまま同じ所を読み直さないように) */
static int synerr(const char *a)
{
    err(a, 0);
    g_exit = 1;
    g_exit_code = 2;
    return 2;
}

/* ================================================================== */
/* 字句                                                                */
/* ================================================================== */
static int lex(void)
{
    char *s = src;
    unsigned pi = 0;

    ntok = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (ntok >= TOK_MAX - 1) return err("script too long (tokens)", 0);
        if (*s == '#') { while (*s && *s != '\n') s++; continue; }
        if (!*s) break;
        if (*s == '\\' && s[1] == '\n') { s += 2; continue; }        /* 行の継続 */
        if (*s == '\n' || *s == ';') {
            if (s[0] == ';' && s[1] == ';') { ttype[ntok++] = T_DSEMI; s += 2; continue; }
            ttype[ntok++] = T_NL; s++; continue;
        }
        if (*s == '&') { if (s[1] == '&') { ttype[ntok++] = T_AND; s += 2; } else { ttype[ntok++] = T_AMP; s++; } continue; }
        if (*s == '|') { if (s[1] == '|') { ttype[ntok++] = T_OR; s += 2; } else { ttype[ntok++] = T_PIPE; s++; } continue; }
        if (*s == '>') { if (s[1] == '>') { ttype[ntok++] = T_GTGT; s += 2; } else { ttype[ntok++] = T_GT; s++; } continue; }
        if (*s == '<') { ttype[ntok++] = T_LT; s++; continue; }
        if (*s == '(') { ttype[ntok++] = T_LP; s++; continue; }
        if (*s == ')') { ttype[ntok++] = T_RP; s++; continue; }
        /* 語: 引用符と $(( … )) の中は区切らない */
        ttype[ntok] = T_WORD;
        tword[ntok] = (unsigned short)pi;
        while (*s && !strchr(" \t\r\n;&|<>()", *s)) {
            if (*s == '\'' || *s == '"') {
                char q = *s;
                pool[pi++] = *s++;
                while (*s && *s != q) {
                    if (q == '"' && *s == '\\' && s[1]) pool[pi++] = *s++;
                    pool[pi++] = *s++;
                }
                if (!*s) return err("unterminated quote", 0);
                pool[pi++] = *s++;
            } else if (*s == '\\' && s[1]) {
                pool[pi++] = *s++;
                pool[pi++] = *s++;
            } else if (*s == '$' && s[1] == '(') {
                int depth = 0;
                pool[pi++] = *s++;                       /* $ */
                do {
                    if (*s == '(') depth++;
                    else if (*s == ')') depth--;
                    pool[pi++] = *s++;
                } while (*s && depth > 0);
                if (depth) return err("unterminated $(", 0);
            } else
                pool[pi++] = *s++;
            if (pi >= sizeof pool - 2) return err("script too long", 0);
        }
        pool[pi++] = 0;
        ntok++;
    }
    ttype[ntok] = T_EOF;
    return 0;
}

static const char *traw(int i) { return pool + tword[i]; }
static int is_kw(int i, const char *k) { return ttype[i] == T_WORD && strcmp(traw(i), k) == 0; }

/* ================================================================== */
/* 変数                                                                */
/* ================================================================== */
static int is_name(const char *s, int n)
{
    int i;
    if (n <= 0 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return 0;
    for (i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return 0;
    return 1;
}

static const char *var_get(const char *name, int n)
{
    int i;
    for (i = 0; i < VAR_MAX; i++)
        if (vname[i][0] && (int)strlen(vname[i]) == n && t_strncmp(vname[i], name, (unsigned)n) == 0)
            return vval[i];
    return "";
}

static void var_set(const char *name, int n, const char *val)
{
    int i, f = -1;
    if (n >= VNAME_MAX) n = VNAME_MAX - 1;
    for (i = 0; i < VAR_MAX; i++) {
        if (vname[i][0] && (int)strlen(vname[i]) == n && t_strncmp(vname[i], name, (unsigned)n) == 0) { f = i; break; }
        if (!vname[i][0] && f < 0) f = i;
    }
    if (f < 0) { err("too many variables", 0); return; }
    t_memcpy(vname[f], name, (unsigned)n);
    vname[f][n] = 0;
    t_strlcpy(vval[f], val, VVAL_MAX);
}

static void utoa10(long v, char *out)
{
    char b[12];
    int i = 0, neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { b[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) *out++ = '-';
    while (i) *out++ = b[--i];
    *out = 0;
}

/* ================================================================== */
/* 算術 $(( … ))                                                        */
/* ================================================================== */
static const char *ap;
static long a_expr(void);

static void a_sp(void) { while (*ap == ' ') ap++; }

static long a_prim(void)
{
    long v = 0;
    a_sp();
    if (*ap == '(') { ap++; v = a_expr(); a_sp(); if (*ap == ')') ap++; return v; }
    if (*ap == '-') { ap++; return -a_prim(); }
    if (*ap == '!') { ap++; return !a_prim(); }
    if (*ap >= '0' && *ap <= '9') { while (*ap >= '0' && *ap <= '9') v = v * 10 + (*ap++ - '0'); return v; }
    if (*ap == '$') ap++;
    {
        const char *b = ap, *s;
        int neg = 0;
        while ((*ap >= 'a' && *ap <= 'z') || (*ap >= 'A' && *ap <= 'Z') || (*ap >= '0' && *ap <= '9') || *ap == '_') ap++;
        s = (ap - b == 1 && *b >= '1' && *b <= '9') ? (*b - '0' < pargc ? pargv[*b - '0'] : "") : var_get(b, (int)(ap - b));
        if (*s == '-') { neg = 1; s++; }
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
        return neg ? -v : v;
    }
}
static long a_mul(void)
{
    long v = a_prim(), r;
    for (;;) {
        a_sp();
        if (*ap == '*') { ap++; v *= a_prim(); }
        else if (*ap == '/' || *ap == '%') {
            char o = *ap++;
            r = a_prim();
            if (!r) { err("division by zero", 0); return 0; }
            v = o == '/' ? v / r : v % r;
        } else return v;
    }
}
static long a_add(void)
{
    long v = a_mul();
    for (;;) {
        a_sp();
        if (*ap == '+') { ap++; v += a_mul(); }
        else if (*ap == '-') { ap++; v -= a_mul(); }
        else return v;
    }
}
static long a_cmp(void)
{
    long v = a_add();
    for (;;) {
        a_sp();
        if (ap[0] == '<' && ap[1] == '=') { ap += 2; v = v <= a_add(); }
        else if (ap[0] == '>' && ap[1] == '=') { ap += 2; v = v >= a_add(); }
        else if (ap[0] == '=' && ap[1] == '=') { ap += 2; v = v == a_add(); }
        else if (ap[0] == '!' && ap[1] == '=') { ap += 2; v = v != a_add(); }
        else if (*ap == '<') { ap++; v = v < a_add(); }
        else if (*ap == '>') { ap++; v = v > a_add(); }
        else return v;
    }
}
static long a_expr(void)
{
    long v = a_cmp(), r;
    for (;;) {
        a_sp();
        if (ap[0] == '&' && ap[1] == '&') { ap += 2; r = a_cmp(); v = v && r; }
        else if (ap[0] == '|' && ap[1] == '|') { ap += 2; r = a_cmp(); v = v || r; }
        else return v;
    }
}

/* ================================================================== */
/* 語の展開(引用符を外し、$ を置き換える)                              */
/* ================================================================== */
static unsigned put(char *out, unsigned o, const char *s)
{
    while (*s && o < WORD_MAX - 1) out[o++] = *s++;
    return o;
}

/* raw を out へ展開する。quoted に「引用符の中だった」を返す(for の語の分割に使う) */
static void expand(const char *raw, char *out, int *quoted)
{
    unsigned o = 0;
    char q = 0, nb[12];

    if (quoted) *quoted = 0;
    while (*raw && o < WORD_MAX - 1) {
        char c = *raw;
        if (!q && (c == '\'' || c == '"')) { q = c; raw++; if (quoted) *quoted = 1; continue; }
        if (q && c == q) { q = 0; raw++; continue; }
        if (q == '\'') { out[o++] = *raw++; continue; }
        if (c == '\\' && raw[1]) { raw++; out[o++] = *raw++; continue; }
        if (c == '$' && raw[1] == '(' && raw[2] == '(') {       /* $(( 式 )) */
            const char *e;
            char ex[WORD_MAX];
            unsigned k = 0;
            int depth = 2;
            raw += 3;
            for (e = raw; *e && depth > 0; e++) {
                if (*e == '(') depth++;
                else if (*e == ')') depth--;
                if (depth > 0 && k < sizeof ex - 1) ex[k++] = *e;
            }
            if (k && ex[k - 1] == ')') k--;
            ex[k] = 0;
            ap = ex;
            utoa10(a_expr(), nb);
            o = put(out, o, nb);
            raw = e;
            continue;
        }
        if (c == '$' && raw[1] == '(') { err("$(...) is not supported", 0); raw++; continue; }
        if (c == '$') {
            const char *b;
            raw++;
            if (*raw == '?') { utoa10(last_status, nb); o = put(out, o, nb); raw++; continue; }
            if (*raw == '#') { utoa10(pargc > 0 ? pargc - 1 : 0, nb); o = put(out, o, nb); raw++; continue; }
            if (*raw == '@' || *raw == '*') {
                int i;
                for (i = 1; i < pargc; i++) { if (i > 1) o = put(out, o, " "); o = put(out, o, pargv[i]); }
                raw++;
                continue;
            }
            if (*raw >= '0' && *raw <= '9') {
                int i = *raw++ - '0';
                if (i < pargc) o = put(out, o, pargv[i]);
                continue;
            }
            if (*raw == '{') {
                b = ++raw;
                while (*raw && *raw != '}') raw++;
                o = put(out, o, var_get(b, (int)(raw - b)));
                if (*raw) raw++;
                continue;
            }
            b = raw;
            while ((*raw >= 'a' && *raw <= 'z') || (*raw >= 'A' && *raw <= 'Z') || (*raw >= '0' && *raw <= '9') || *raw == '_') raw++;
            if (raw == b) { out[o++] = '$'; continue; }
            o = put(out, o, var_get(b, (int)(raw - b)));
            continue;
        }
        out[o++] = *raw++;
    }
    out[o] = 0;
}

/* ================================================================== */
/* test / [                                                            */
/* ================================================================== */
static long tnum(const char *s)
{
    long v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static int is_dir(const char *p)
{
    if (opendir(p) != 0) return 0;
    closedir();
    return 1;
}

static int is_file(const char *p)
{
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int do_test(int ac, char **av)
{
    int neg = 0, r;
    if (ac > 0 && strcmp(av[0], "!") == 0) { neg = 1; av++; ac--; }
    if (ac == 0) r = 0;
    else if (ac == 1) r = av[0][0] != 0;
    else if (ac == 2) {
        if (!strcmp(av[0], "-z")) r = av[1][0] == 0;
        else if (!strcmp(av[0], "-n")) r = av[1][0] != 0;
        else if (!strcmp(av[0], "-f")) r = is_file(av[1]) && !is_dir(av[1]);
        else if (!strcmp(av[0], "-d")) r = is_dir(av[1]);
        else if (!strcmp(av[0], "-e")) r = is_file(av[1]) || is_dir(av[1]);
        else return err("test: unknown operator ", av[0]);
    } else if (ac == 3) {
        const char *o = av[1];
        if (!strcmp(o, "=") || !strcmp(o, "==")) r = strcmp(av[0], av[2]) == 0;
        else if (!strcmp(o, "!=")) r = strcmp(av[0], av[2]) != 0;
        else if (!strcmp(o, "-eq")) r = tnum(av[0]) == tnum(av[2]);
        else if (!strcmp(o, "-ne")) r = tnum(av[0]) != tnum(av[2]);
        else if (!strcmp(o, "-lt")) r = tnum(av[0]) < tnum(av[2]);
        else if (!strcmp(o, "-le")) r = tnum(av[0]) <= tnum(av[2]);
        else if (!strcmp(o, "-gt")) r = tnum(av[0]) > tnum(av[2]);
        else if (!strcmp(o, "-ge")) r = tnum(av[0]) >= tnum(av[2]);
        else return err("test: unknown operator ", o);
    } else
        return err("test: too many arguments", 0);
    return (r ^ neg) ? 0 : 1;
}

/* ================================================================== */
/* 単純コマンドの実行                                                  */
/* ================================================================== */
#define BIN_PATH_MAX 48
static void bin_path(const char *cmd, char *f)
{
    unsigned j = 0, i;
    if (cmd[0] != '/') { t_memcpy(f, "/bin/", 5); j = 5; }
    for (i = 0; cmd[i] && j < BIN_PATH_MAX - 6; i++) f[j++] = cmd[i];
    t_memcpy(f + j, ".bin", 5);
}

static unsigned pack(int ac, char **av, char *pk)
{
    unsigned o = 0, k;
    int i;
    for (i = 1; i < ac; i++) {
        k = (unsigned)strlen(av[i]) + 1;
        if (o + k >= PACK_MAX) break;
        t_memcpy(pk + o, av[i], k);
        o += k;
    }
    pk[o] = 0;
    return o;
}

/* 外部コマンドを起こす。戻り: スロット / 0 = 空き無し / 0xFF = ファイル無し */
static unsigned char launch(int ac, char **av)
{
    static char pk[PACK_MAX];
    char fn[BIN_PATH_MAX];
    bin_path(av[0], fn);
    pack(ac, av, pk);
    return kexec_argv(fn, pk, (unsigned char)(ac - 1));
}

static int wait_fg(unsigned char n)
{
    while (SH_PID(n) != 0) {
        if (kcon_break()) {
            SH_KILL(n);
            printf("\n^C\n");
            g_exit = 1;
            g_exit_code = 130;
            return 130;
        }
        ksleep(1);
    }
    return (int)syscall5(52, n, 0, 0, 0);
}

static int run_builtin(int ac, char **av, int *handled)
{
    const char *c = av[0];
    int i;

    *handled = 1;
    if (!strcmp(c, "echo")) {
        int nl = 1, f = 1;
        if (ac > 1 && !strcmp(av[1], "-n")) { nl = 0; f = 2; }
        for (i = f; i < ac; i++) { if (i > f) putchar(' '); prs(av[i]); }
        if (nl) putchar('\n');
        return 0;
    }
    if (!strcmp(c, "test")) return do_test(ac - 1, av + 1);
    if (!strcmp(c, "[")) {
        if (strcmp(av[ac - 1], "]")) return err("[: missing ]", 0);
        return do_test(ac - 2, av + 1);
    }
    if (!strcmp(c, "true") || !strcmp(c, ":")) return 0;
    if (!strcmp(c, "false")) return 1;
    if (!strcmp(c, "exit")) { g_exit = 1; g_exit_code = ac > 1 ? (int)tnum(av[1]) : last_status; return g_exit_code; }
    if (!strcmp(c, "break")) { g_break = ac > 1 ? (int)tnum(av[1]) : 1; return 0; }
    if (!strcmp(c, "continue")) { g_continue = 1; return 0; }
    if (!strcmp(c, "shift")) {
        int k = ac > 1 ? (int)tnum(av[1]) : 1;
        while (k-- > 0 && pargc > 1) { for (i = 1; i < pargc - 1; i++) pargv[i] = pargv[i + 1]; pargc--; }
        return 0;
    }
    if (!strcmp(c, "cd")) return kchdir(ac > 1 ? av[1] : "/root") == 0 ? 0 : (err("cd: no such dir ", ac > 1 ? av[1] : ""), 1);
    if (!strcmp(c, "pwd")) { char cw[48]; kgetcwd(cw); puts(cw); return 0; }
    if (!strcmp(c, "read")) {
        char line[VVAL_MAX];
        unsigned k = 0;
        int ch;
        while ((ch = getchar()) >= 0 && ch != '\n' && ch != '\r')
            if (k < sizeof line - 1) line[k++] = (char)ch;
        line[k] = 0;
        if (ac > 1) var_set(av[1], (int)strlen(av[1]), line);
        return ch < 0 && k == 0 ? 1 : 0;
    }
    if (builtin_is(c)) {                          /* カーネルの組み込み(ps / kill …) */
        char args[PACK_MAX];
        unsigned o = 0;
        for (i = 1; i < ac; i++) {
            if (i > 1 && o < sizeof args - 1) args[o++] = ' ';
            o += (unsigned)strlen(t_strlcpy(args + o, av[i], sizeof args - o));
        }
        args[o] = 0;
        builtin_try(c, args);
        return 0;
    }
    *handled = 0;
    return 0;
}

static int is_mybuiltin(const char *c)
{
    static const char *const names[] = { "echo", "test", "[", "true", "false", ":", "exit", "break",
                                         "continue", "shift", "cd", "pwd", "read", 0 };
    int i;
    for (i = 0; names[i]; i++)
        if (!strcmp(c, names[i])) return 1;
    return 0;
}

#define PIPE_TMP "/tzsh.pip"

static int run_simple(int ac, char **av, int bg)
{
    int handled, st;
    unsigned char n;

    st = run_builtin(ac, av, &handled);
    if (handled) return st;
    n = launch(ac, av);
    if (n == 0xFF) { printf("tzsh: %s: not found\n", av[0]); return 127; }
    if (n == 0) { printf("tzsh: %s: no free slot\n", av[0]); return 126; }
    if (bg) return 0;
    return wait_fg(n);
}

/* ================================================================== */
/* 構文(再帰下降。exec = 0 のときは読むだけ)                           */
/* ================================================================== */
static int run_list(int exec);

static int at_end(int i)
{
    if (ttype[i] == T_EOF || ttype[i] == T_DSEMI || ttype[i] == T_RP) return 1;
    return is_kw(i, "then") || is_kw(i, "elif") || is_kw(i, "else") || is_kw(i, "fi")
        || is_kw(i, "do") || is_kw(i, "done") || is_kw(i, "esac");
}

static void skip_nl(void) { while (ttype[pos] == T_NL) pos++; }

static int expect(const char *kw)
{
    skip_nl();
    if (!is_kw(pos, kw)) { err("syntax error: expected ", kw); g_exit = 1; g_exit_code = 2; return 0; }
    pos++;
    return 1;
}

static int stopped(void) { return g_exit || g_break || g_continue; }

/* 単純コマンド(と a | b、リダイレクト、&) */
static int run_pipeline(int exec)
{
    static char wbuf[2][ARG_MAX][WORD_MAX];
    char *av[2][ARG_MAX + 1];
    int ac[2] = { 0, 0 }, side = 0, bg = 0, st = 0, i, assign_only;
    char rfile[WORD_MAX];
    int rkind = 0;                                   /* 1 = > / 2 = >> / 3 = < */

    rfile[0] = 0;
    while (ttype[pos] != T_EOF && ttype[pos] != T_NL && ttype[pos] != T_AMP && ttype[pos] != T_AND
           && ttype[pos] != T_OR && ttype[pos] != T_DSEMI && ttype[pos] != T_RP
           && !(ac[side] == 0 && at_end(pos))) {
        int t = ttype[pos];
        if (t == T_PIPE) { if (side == 1) return synerr("only one | is supported"); side = 1; pos++; continue; }
        if (t == T_GT || t == T_GTGT || t == T_LT) {
            pos++;
            if (ttype[pos] != T_WORD) return synerr("syntax error near redirection");
            if (exec) expand(traw(pos), rfile, 0);
            rkind = t == T_GT ? 1 : t == T_GTGT ? 2 : 3;
            pos++;
            continue;
        }
        if (t == T_LP) return synerr("syntax error near (");
        if (ac[side] < ARG_MAX && exec)
            expand(traw(pos), wbuf[side][ac[side]], 0);
        if (ac[side] < ARG_MAX) { av[side][ac[side]] = wbuf[side][ac[side]]; ac[side]++; }
        pos++;
    }
    if (ttype[pos] == T_AMP) { bg = 1; pos++; }
    if (!exec) return 0;
    av[0][ac[0]] = 0;
    av[1][ac[1]] = 0;

    /* 名前=値 だけの行 */
    assign_only = ac[0] > 0 && side == 0;
    for (i = 0; i < ac[0] && assign_only; i++) {
        char *e = strchr(av[0][i], '=');
        if (!e || !is_name(av[0][i], (int)(e - av[0][i]))) assign_only = 0;
    }
    if (assign_only) {
        for (i = 0; i < ac[0]; i++) {
            char *e = strchr(av[0][i], '=');
            var_set(av[0][i], (int)(e - av[0][i]), e + 1);
        }
        return 0;
    }
    if (ac[0] == 0) return 0;

    if (side == 1) {                                 /* a | b */
        static char pl[PACK_MAX], pr[PACK_MAX];
        if (ac[1] == 0) return synerr("syntax error near |");
        if (is_mybuiltin(av[0][0]) || is_mybuiltin(av[1][0]) || builtin_is(av[0][0]) || builtin_is(av[1][0])) {
            /* 片側が組み込み: 一時ファイル経由(sh と同じ。左の出力 → ファイル → 右の入力) */
            if (redir_begin(PIPE_TMP, 0) != 0) return 1;
            run_simple(ac[0], av[0], 0);
            redir_end();
            if (in_begin(PIPE_TMP) != 0) return 1;
            st = run_simple(ac[1], av[1], 0);
            in_end();
            unlink(PIPE_TMP);
            return st;
        }
        pack(ac[0], av[0], pl);
        pack(ac[1], av[1], pr);
        st = krun_pipe(av[0][0], pl, (unsigned char)(ac[0] - 1), av[1][0], pr, (unsigned char)(ac[1] - 1));
        if (st == 1) printf("tzsh: cannot start pipe\n");
        else if (st == 3) { g_exit = 1; g_exit_code = 130; }
        return st;
    }

    if (rkind == 1 || rkind == 2) {
        if (redir_begin(rfile, rkind == 2) != 0) return 1;
        st = run_simple(ac[0], av[0], 0);
        redir_end();
        return st;
    }
    if (rkind == 3) {
        if (in_begin(rfile) != 0) return 1;
        st = run_simple(ac[0], av[0], 0);
        in_end();
        return st;
    }
    return run_simple(ac[0], av[0], bg);
}

static int glob_match(const char *p, const char *s)
{
    if (!*p) return !*s;
    if (*p == '*') { do { if (glob_match(p + 1, s)) return 1; } while (*s++); return 0; }
    if (!*s) return 0;
    if (*p == '?' || *p == *s) return glob_match(p + 1, s + 1);
    return 0;
}

static int run_command(int exec)
{
    int st = 0;

    skip_nl();
    if (is_kw(pos, "!")) { pos++; st = run_command(exec); return exec ? !st : 0; }

    if (is_kw(pos, "if")) {
        int done = 0, c;
        pos++;
        c = run_list(exec);
        if (!expect("then")) return 2;
        if (exec && !stopped() && c == 0) { st = run_list(1); done = 1; } else run_list(0);
        for (;;) {
            skip_nl();
            if (is_kw(pos, "elif")) {
                pos++;
                c = run_list(exec && !done && !stopped());
                if (!expect("then")) return 2;
                if (exec && !done && !stopped() && c == 0) { st = run_list(1); done = 1; } else run_list(0);
            } else if (is_kw(pos, "else")) {
                pos++;
                if (exec && !done && !stopped()) st = run_list(1); else run_list(0);
            } else break;
        }
        expect("fi");
        return st;
    }

    if (is_kw(pos, "while") || is_kw(pos, "until")) {
        int until = is_kw(pos, "until"), start = ++pos, c;
        for (;;) {
            pos = start;
            c = run_list(exec);
            if (!expect("do")) return 2;
            if (exec && !stopped() && ((c == 0) ^ until)) {
                st = run_list(1);
                expect("done");
                if (g_continue) g_continue = 0;
                if (g_break) { g_break--; break; }
                if (g_exit) break;
                continue;
            }
            run_list(0);
            expect("done");
            break;
        }
        return st;
    }

    if (is_kw(pos, "for")) {
        char name[VNAME_MAX], items[ARG_MAX][WORD_MAX];
        int n = 0, i, start;
        pos++;
        if (ttype[pos] != T_WORD) return synerr("syntax error after for");
        t_strlcpy(name, traw(pos), VNAME_MAX);
        pos++;
        if (is_kw(pos, "in")) {
            pos++;
            while (ttype[pos] == T_WORD) {
                if (exec) {
                    char w[WORD_MAX], *p;
                    int q;
                    expand(traw(pos), w, &q);
                    p = w;
                    while (*p && n < ARG_MAX) {              /* 引用符の無い語は空白で割る */
                        char *b;
                        if (!q) while (*p == ' ') p++;
                        if (!*p) break;
                        b = p;
                        if (!q) while (*p && *p != ' ') p++; else p += strlen(p);
                        t_memcpy(items[n], b, (unsigned)(p - b));
                        items[n][p - b] = 0;
                        n++;
                    }
                }
                pos++;
            }
        } else
            for (i = 1; i < pargc && n < ARG_MAX; i++) strcpy(items[n++], pargv[i]);
        if (!expect("do")) return 2;
        start = pos;
        if (!exec || n == 0) { run_list(0); expect("done"); return 0; }
        for (i = 0; i < n; i++) {
            pos = start;
            var_set(name, (int)strlen(name), items[i]);
            st = run_list(1);                        /* 止まっても done の手前まで読み進む */
            if (g_continue) g_continue = 0;
            if (g_break || g_exit) {
                if (g_break) g_break--;
                break;
            }
        }
        expect("done");
        return st;
    }

    if (is_kw(pos, "case")) {
        char word[WORD_MAX], pat[WORD_MAX];
        int matched = 0;
        pos++;
        if (ttype[pos] != T_WORD) return synerr("syntax error after case");
        if (exec) expand(traw(pos), word, 0);
        pos++;
        if (!expect("in")) return 2;
        for (;;) {
            int hit = 0;
            skip_nl();
            if (is_kw(pos, "esac") || ttype[pos] == T_EOF) break;
            if (ttype[pos] == T_LP) pos++;
            for (;;) {                                   /* 型|型|… ) */
                if (ttype[pos] != T_WORD) return synerr("syntax error in case pattern");
                if (exec && !matched) { expand(traw(pos), pat, 0); if (glob_match(pat, word)) hit = 1; }
                pos++;
                if (ttype[pos] == T_PIPE) { pos++; continue; }
                break;
            }
            if (ttype[pos] != T_RP) return synerr("syntax error: expected ) in case");
            pos++;
            if (exec && !matched && hit && !stopped()) { st = run_list(1); matched = 1; } else run_list(0);
            skip_nl();
            if (ttype[pos] == T_DSEMI) pos++;
        }
        expect("esac");
        return st;
    }

    return run_pipeline(exec);
}

/* && と || でつないだ並び */
static int run_andor(int exec)
{
    int st = run_command(exec);
    for (;;) {
        if (ttype[pos] == T_AND) { pos++; skip_nl(); st = (exec && !stopped() && st == 0) ? run_command(1) : (run_command(0), st); }
        else if (ttype[pos] == T_OR) { pos++; skip_nl(); st = (exec && !stopped() && st != 0) ? run_command(1) : (run_command(0), st); }
        else return st;
    }
}

static int run_list(int exec)
{
    int st = 0, p0;
    for (;;) {
        skip_nl();
        if (at_end(pos)) return st;
        p0 = pos;
        st = run_andor(exec && !stopped());
        if (pos == p0) {                             /* 1 トークンも読めなかった = 書き方の誤り */
            if (!g_exit) synerr(ttype[pos] == T_WORD ? "syntax error near a word" : "syntax error near an operator");
            return st;
        }
        if (exec && !stopped()) last_status = st;
        if (ttype[pos] == T_NL || ttype[pos] == T_AMP) pos++;
    }
}

/* ================================================================== */
int main(int argc, char **argv)
{
    unsigned n = 0;
    int i, a0;

    if (argc < 1) {
        puts("usage: tzsh FILE [args...] | tzsh -c 'commands' [args...]");
        return 2;
    }
    if (!strcmp(argv[0], "-c")) {
        if (argc < 2) return err("-c needs a string", 0);
        t_strlcpy(src, argv[1], SRC_MAX + 1);
        n = (unsigned)strlen(src);
        a0 = 1;
    } else {
        FILE *f = fopen(argv[0], "r");
        int c;
        if (!f) return err("cannot open ", argv[0]);
        while ((c = fgetc(f)) >= 0 && n < SRC_MAX) src[n++] = (char)c;
        fclose(f);
        if (n >= SRC_MAX) return err("script too long", 0);
        a0 = 0;
    }
    src[n] = 0;
    pargc = 0;
    for (i = a0; i < argc && pargc < 10; i++) pargv[pargc++] = argv[i];   /* $0 = スクリプト名 */

    if (lex()) return 2;
    pos = 0;
    run_list(1);
    if (ttype[pos] != T_EOF && !g_exit) {
        err("syntax error near ", ttype[pos] == T_WORD ? traw(pos) : "operator");
        return 2;
    }
    return g_exit ? g_exit_code : last_status;
}
