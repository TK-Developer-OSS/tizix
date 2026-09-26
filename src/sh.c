#include <string.h>

#include "builtin.h"
#include "kexec.h"
#include "fatcmd.h"
#include "vfs.h"
#include "kmem.h"
#include "pipe.h"       /* 両側外部コマンドの A | B をカーネルパイプで繋ぐ */

#define CWD_MAX   48                 /* カレントディレクトリ文字列上限(FAT12 8.3 数段) */

#include "io.h"
#include "sh.h"

#define LINE_MAX  48

#define PIDTBL   ((volatile unsigned char *)KW_PIDTAB)    /* pid_tbl[block] 0=free   */
#define OUTROUTE ((volatile unsigned char *)KW_OUTROUTE)  /* out_route[block]        */
#define ROUTE_CONSOLE  0
#define ROUTE_DISCARD  1

static char line[LINE_MAX];

/* ================================================================== */
/* カレントディレクトリ (pwd / cd)                                     */
/*   旧 cd/pwd は偽ツリー移動だった(VFS 整理時に削除)。今回は実 FAT   */
/*   上の cwd 文字列をシェルが保持し、相対パスは sh がディスパッチ前に */
/*   絶対化する(集中解決)。FatFs 側は無改造(FF_FS_RPATH=0 のまま)。  */
/* ================================================================== */

static char cwd[CWD_MAX];            /* 例: "/" , "/AAA" , "/AAA/BBB"。BSS なので sh() で初期化 */
static char argbuf[CWD_MAX * 2 + 10]; /* 相対→絶対 変換後の引数(mv/cp は "SRC DST" 2本、
                                        * dd は "if=SRC of=DST" で prefix 分 +6 要る) */
static char redirbuf[CWD_MAX];       /* > / < のファイル名の絶対化用 */
static char pipebuf[CWD_MAX];        /* パイプ左辺の引数絶対化(右辺は argbuf 共用) */
static char prompt[CWD_MAX + 8];     /* "[" + cwd + "]# " + NUL */

/* path_norm : base(絶対パス)を起点に arg を解決し out へ絶対パスを書く。
 *   ・arg が '/' 始まりなら base を無視してルートから
 *   ・"." は無視、".." は 1 階層戻る(root より上には行かない)
 *   ・セグメント区切りは '/'。余分な '/' は畳む。
 *   戻り 0=ok / -1=out(CWD_MAX)に収まらない。
 *   掟に従い符号なしのみ・除算/乗算なし。 */
static int path_norm(const char *base, const char *arg, char *out)
{
    unsigned char len;
    const char *p = arg;

    while (*p == ' ') p++;

    if (*p == '/') {                 /* 絶対指定: ルートから組み立て直す */
        out[0] = '/'; out[1] = 0;
        while (*p == '/') p++;
    } else {                         /* 相対指定: base をコピーして継ぎ足す */
        len = 0;
        while (base[len] && len < CWD_MAX - 1) { out[len] = base[len]; len++; }
        out[len] = 0;
    }

    while (*p) {
        const char *s = p;
        unsigned char n = 0;
        while (*p && *p != '/') { p++; n++; }   /* [s, s+n) が 1 セグメント */

        if (n == 1 && s[0] == '.') {
            /* カレント: 何もしない */
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            len = 0; while (out[len]) len++;
            while (len > 0 && out[len - 1] != '/') len--;  /* 末尾セグメントを削る */
            if (len > 1) len--;                            /* 区切り '/' も(root '/' は残す) */
            out[len] = 0;
        } else if (n > 0) {
            len = 0; while (out[len]) len++;
            if ((unsigned int)len + 1 + n > CWD_MAX - 1) return -1;
            if (len == 0 || out[len - 1] != '/') out[len++] = '/';
            { unsigned char k; for (k = 0; k < n; k++) out[len++] = s[k]; }
            out[len] = 0;
        }
        while (*p == '/') p++;
    }

    if (out[0] == 0) { out[0] = '/'; out[1] = 0; }
    return 0;
}

/* cd <dir> : cwd を更新。無引数はホーム "/root" へ。相対/絶対/"."/".." に対応。
 *   ターゲットは path_norm で正規化後 vfs_resolve で backend 判定:
 *     FAT     … fat_isdir で実在ディレクトリか確認
 *     DEVFS   … vnode が VT_DIR(= /dev)なら可
 *     ENOENT  … 不可
 *   実在するディレクトリでなければ cwd は変えない。 */
static void do_cd(const char *arg)
{
    unsigned char i;
    int v;
    const char *shown;

    while (*arg == ' ') arg++;
    shown = *arg ? arg : "/root";        /* 無引数 cd はホーム */
    if (path_norm(cwd, shown, argbuf) != 0) {
        kprintf("cd: path too long\n");
        return;
    }

    v = vfs_resolve(argbuf);
    if (v == VFS_FAT) {
        if (!fat_isdir(argbuf)) {
            kprintf("cd: %s: no such dir\n", shown);
            return;
        }
    } else if (v == VFS_ENOENT) {
        kprintf("cd: %s: no such dir\n", shown);
        return;
    } else {                              /* DEVFS ノード */
        struct vnode *vn = vfs_node(v);
        if (!vn || vn->type != VT_DIR) {
            kprintf("cd: %s: not a directory\n", shown);
            return;
        }
    }

    for (i = 0; argbuf[i] && i < CWD_MAX - 1; i++)
        cwd[i] = argbuf[i];
    cwd[i] = 0;
}

/* resolve_arg : パスを取るビルトインの引数を cwd 起点で絶対化して返す。
 *   対象外コマンド / 既に絶対パス / 引数無し はそのまま返す。
 *   呼び出しは cwd が "/" 以外のときだけ(ルート時は完全にゼロコスト)。
 *
 *   ★これは「どの引数がパスか」を sh がコマンドごとに推測するヒューリス
 *   ティクスで、z80 の user/sh.c が #28 で明示的に捨てた設計(`wc -l` が
 *   "cannot open -l" になる等、コマンドにオプションが増えるたび壊れる)。
 *   x86-ia16/m68k-mega はどちらもカーネル側に kpath() 相当の cwd 解決を
 *   持たない(cwd は sh.c 自身の static。z80 のような kernel-owned cwd
 *   ではない)ためこれに頼らざるを得ない。コマンドを追加/変更するたびに
 *   ここのフラグ形状を合わせて直すこと(1箇所忘れると即座に壊れる)。 */
static char *resolve_arg(const char *cmd, char *arg)
{
    if (strcmp(cmd, "mv") == 0 || strcmp(cmd, "cp") == 0) {
        char a[16], b[16];
        unsigned char n;
        char *w;
        const char *r = arg;

        while (*r == ' ') r++;
        n = 0; while (*r && *r != ' ' && n < 15) a[n++] = *r++; a[n] = 0;
        while (*r == ' ') r++;
        n = 0; while (*r && *r != ' ' && n < 15) b[n++] = *r++; b[n] = 0;
        if (!a[0] || !b[0]) return arg;

        if (path_norm(cwd, a, argbuf) != 0) return arg;
        w = argbuf; while (*w) w++;
        if (path_norm(cwd, b, w + 1) != 0) return arg;
        *w = ' ';
        return argbuf;
    }
    if (strcmp(cmd, "ls") == 0) {
        /* "-l"/"-h" 等のフラグトークンは絶対化対象ではない。先頭から並ぶ
         * "-" 始まりのトークンはそのまま argbuf へ写し、残ったパス部分
         * (あれば)だけ path_norm で絶対化する。
         * ★2026-09-12: 以前はフラグの有無を考えずに arg 全体を 1 個の
         *   パスとして path_norm に渡していたため、`ls -l /root` のように
         *   フラグ+パスを両方書くと "-l /root" をまるごと 1 セグメント扱い
         *   して壊れたパスになっていた(z80pack でも x86 でも再現する
         *   共通コード側のバグ)。 */
        char *p = arg;
        char *w = argbuf;
        while (*p == ' ') p++;
        while (*p == '-') {
            while (*p && *p != ' ') *w++ = *p++;
            *w++ = ' ';
            while (*p == ' ') p++;
        }
        if (!*p) {                              /* パス省略 = cwd を列挙 */
            unsigned char i;
            if (w == argbuf) return cwd;         /* フラグも無し: 完全に無引数 */
            /* フラグのみ("ls -l" 等): ls.c 側の既定値 "." に頼らず cwd を
             * 明示で付ける。★2026-09-12: x86(src/sh.c 系統)は z80 の
             * kpath() に相当するカーネル側の相対パス解決を持たないため、
             * ls.c が渡す "." をそのまま FatFs に渡しても解決できず
             * "cannot open" になっていた。 */
            for (i = 0; cwd[i] && i < CWD_MAX - 1; i++) *w++ = cwd[i];
            *w = 0;
            return argbuf;
        }
        if (path_norm(cwd, p, w) != 0) return arg;
        return argbuf;
    }
    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "rm") == 0 ||
        strcmp(cmd, "mkdir") == 0 || strcmp(cmd, "touch") == 0 ||
        strcmp(cmd, "wc") == 0 || strcmp(cmd, "uniq") == 0 ||
        strcmp(cmd, "tee") == 0 || strcmp(cmd, "du") == 0 ||
        strcmp(cmd, "rmdir") == 0 || strcmp(cmd, "vi") == 0) {
        /* #81: 先頭のフラグ(wc -l 等)はそのまま写し、残りのトークンを 1 個ずつ
         * 絶対化する。以前は引数全体を 1 本のパスとして path_norm に渡していたので、
         * `wc -l f` が "<cwd>/-l f"、`rm a b` が "<cwd>/a b" になっていた。 */
        char tok[CWD_MAX];
        char *p = arg;
        char *w = argbuf;
        unsigned char n;

        while (*p == ' ') p++;
        if (!*p) return arg;
        while (*p == '-') {
            while (*p && *p != ' ') *w++ = *p++;
            *w++ = ' ';
            while (*p == ' ') p++;
        }
        while (*p) {
            n = 0;
            while (*p && *p != ' ' && n < CWD_MAX - 1) tok[n++] = *p++;
            tok[n] = 0;
            while (*p && *p != ' ') p++;         /* CWD_MAX を超えた分は捨てる */
            if ((unsigned)(w - argbuf) + CWD_MAX + 1 > sizeof(argbuf))
                return arg;                      /* 入り切らない: 従来どおり素通し */
            if (path_norm(cwd, tok, w) != 0) return arg;
            while (*w) w++;
            while (*p == ' ') p++;
            if (*p) *w++ = ' ';
        }
        *w = 0;
        return argbuf;
    }
    if (strcmp(cmd, "head") == 0 || strcmp(cmd, "tail") == 0) {
        /* head/tail [-n N] [-N] [FILE] : パスは最後のトークン(あれば)だけ。
         * ★2026-09-12: cat 等と同列に「引数=パス1個」扱いしていたため、
         * `head -2 file` が "-2 file" をまるごと1セグメントの壊れたパス
         * ("<cwd>/-2 file")にしていた(ls -l で既に踏んだのと同じ
         * アンチパターン)。ls と同じくフラグトークンを読み飛ばしてから
         * 残りだけ絶対化する。"-n" だけは直後にもう1トークン(行数)を
         * 消費する点が ls のフラグと違う。 */
        char *p = arg;
        char *w = argbuf;
        while (*p == ' ') p++;
        while (*p == '-') {
            unsigned char is_n = (unsigned char)(p[1] == 'n' && (p[2] == ' ' || p[2] == 0));
            while (*p && *p != ' ') *w++ = *p++;
            *w++ = ' ';
            while (*p == ' ') p++;
            if (is_n) {                          /* "-n" の次の数値トークンも写す */
                while (*p && *p != ' ') *w++ = *p++;
                *w++ = ' ';
                while (*p == ' ') p++;
            }
        }
        if (!*p) { *w = 0; return argbuf; }      /* パス省略(stdin) */
        if (path_norm(cwd, p, w) != 0) return arg;
        return argbuf;
    }
    if (strcmp(cmd, "dd") == 0) {
        /* dd if=SRC of=DST [bs=N count=N skip=N seek=N] : if=/of= の値部分
         * だけ絶対化し、prefix と他の key=value トークンはそのまま写す。
         * トークンは可変個・可変順(dd.c 側が自由な順で解釈するため)。 */
        char *p = arg;
        char *w = argbuf;
        while (*p) {
            char *tok;
            unsigned char is_if, is_of;
            while (*p == ' ') p++;
            if (!*p) break;
            tok = p;
            while (*p && *p != ' ') p++;
            is_if = (unsigned char)(p - tok > 3 && tok[0] == 'i' && tok[1] == 'f' && tok[2] == '=');
            is_of = (unsigned char)(p - tok > 3 && tok[0] == 'o' && tok[1] == 'f' && tok[2] == '=');
            if (is_if || is_of) {
                char save = *p;
                *w++ = tok[0]; *w++ = 'f'; *w++ = '=';
                *p = 0;
                if (path_norm(cwd, tok + 3, w) != 0) { *p = save; return arg; }
                *p = save;
                while (*w) w++;
            } else {
                while (tok < p) *w++ = *tok++;
            }
            if (*p == ' ') *w++ = ' ';
        }
        *w = 0;
        return argbuf;
    }
    if (strcmp(cmd, "grep") == 0 || strcmp(cmd, "sed") == 0) {
        /* grep PATTERN FILE / sed s/OLD/NEW/[g] FILE : 1トークン目はパスでは
         * ないのでそのまま、2トークン目(FILE)だけ絶対化する。cp/mv と同じ
         * 「2トークン目だけ path_norm」の形。 */
        char pat[16];
        unsigned char n;
        char *w;
        const char *r = arg;

        while (*r == ' ') r++;
        n = 0; while (*r && *r != ' ' && n < 15) pat[n++] = *r++; pat[n] = 0;
        while (*r == ' ') r++;
        if (!pat[0] || !*r) return arg;

        w = argbuf;
        { unsigned char i; for (i = 0; pat[i]; i++) *w++ = pat[i]; }
        *w++ = ' ';
        if (path_norm(cwd, r, w) != 0) return arg;
        return argbuf;
    }
    return arg;
}

/* launch: コマンドを /bin から探してロード・プロセス化。
 *   実行ファイルは "/bin/<CMD>.BIN"。先頭 '/' 付き(パス修飾)ならそのまま使う。
 *   戻り = ブロック番号 / 0xFF=ファイル無し / 0=空きブロック無し。 */
/* #48: 引数文字列をトークンに割り、NUL 区切りの argpack(z80 の user/sh.c と同じ
 *   契約 "tok0\0tok1\0…")と個数を作る。"…" は 1 トークンとして中の空白を保ち、
 *   引用符は落とす。以前は生文字列 1 本を kexec_file に渡し、コマンド側の
 *   argv_init.c が空白だけで割り直していたので、`echo "x  y"` が `"x y"` になった。 */
static char packbuf[CWD_MAX * 2 + 10];

static unsigned char pack_args(const char *s)
{
    unsigned char argc = 0;
    unsigned o = 0;

    if (!s) return 0;
    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        while (*s && *s != ' ') {
            if (*s == '"') {
                s++;
                while (*s && *s != '"') {
                    if (o < sizeof(packbuf) - 1) packbuf[o++] = *s;
                    s++;
                }
                if (*s == '"') s++;
            } else {
                if (o < sizeof(packbuf) - 1) packbuf[o++] = *s;
                s++;
            }
        }
        if (o >= sizeof(packbuf)) break;
        packbuf[o++] = 0;
        argc++;
        if (o >= sizeof(packbuf)) break;
    }
    return argc;
}

static unsigned char launch(const char *cmd, const char *arg)
{
    unsigned char i, j;
    char fname[24];

    j = 0;
    if (cmd[0] != '/') {                 /* 無修飾 → /bin/ 固定探索 */
        fname[j++] = '/'; fname[j++] = 'b'; fname[j++] = 'i';
        fname[j++] = 'n'; fname[j++] = '/';
    }
    for (i = 0; cmd[i] && j < 18; i++)
        fname[j++] = cmd[i];
    fname[j++] = '.';
    fname[j++] = 'b';
    fname[j++] = 'i';
    fname[j++] = 'n';
    fname[j]   = 0;

    i = pack_args(arg);
    return kexec_argv(fname, packbuf, i);
}

/* run_kpipe: A | B(両側外部)をカーネルパイプで繋ぐ。
 *   起動〜結線を IRQ_OFF で囲む ── どちらかが pipe_setup 前にスケジュールされると
 *   writer が ROUTE_PIPE 前に putchar / reader が active 前に getchar して
 *   物理コンソールへ漏れる/物理コンソール読みでハングするため。
 *   .BIN ロード(FatFs、ポーリング)は割り込み不要なので di のまま実行してよい。
 *   終了は両方の PID を待つ(Ctrl+C で両方 kill)。 */
static void run_kpipe(const char *cw, const char *aw, const char *cr, const char *ar)
{
    unsigned char wn, rn;

    /* reader を先に起動 → pipe_setup(active=1)。以後 reader が先にスケジュール
     * されても getchar は pipe_getc でブロックするだけ(コンソール誤読しない)。 */
    rn = launch(cr, ar);
    if (rn == 0xFF || rn == 0) {
        kprintf("sh: %s: %s\n", cr, rn ? "not found" : "no free block");
        return;
    }
    /* pipe_setup 失敗(x86/m68k の pipestub は常に 0)を無視すると reader が
     * 物理コンソール読みで待ち続けてハングする。reader を刈って中止。 */
    if (!pipe_setup(rn)) {
        PIDTBL[rn] = 0;
        kprintf("sh: pipe: out of memory\n");
        return;
    }

    /* writer を起動 → wblk 確定 → 最後に ROUTE_PIPE。ここまで writer が先走っても
     * OUTROUTE はまだ CONSOLE なので pipe_putc に入らない(数バイト漏れる程度、稀)。 */
    wn = launch(cw, aw);
    if (wn == 0xFF || wn == 0) {
        pipe_teardown();
        PIDTBL[rn] = 0;
        kprintf("sh: %s: %s\n", cw, wn ? "not found" : "no free block");
        return;
    }
    pipe_attach_writer(wn);
    OUTROUTE[wn] = ROUTE_PIPE;           /* writer の putchar → pipe_putc */

    while (PIDTBL[wn] != 0 || PIDTBL[rn] != 0) {   /* 両方の終了を待つ */
        if (PIDTBL[wn] == 0) pipe_note_exit(wn);   /* writer 終了 → reader へ EOF */
        if (PIDTBL[rn] == 0) {
            pipe_note_exit(rn);                    /* reader 終了 → writer へ rgone */
            /* reader が消えた = writer の出力は捨てられるだけ。SIGPIPE 相当で
             * writer も刈る(`a | echo` のように writer が pipe_putc の -1 を
             * 見ずに無限ループするケースがハングするのを防ぐ)。 */
            if (PIDTBL[wn] != 0)
                PIDTBL[wn] = 0;
        }
        if (con_break()) {
            PIDTBL[wn] = 0;
            PIDTBL[rn] = 0;
            kputchar('\n'); kputchar('^'); kputchar('C'); kputchar('\n');
        }
    }
    OUTROUTE[wn] = ROUTE_CONSOLE;                  /* ルートを戻す(ブロック再利用時の誤 pipe_putc 防止)*/
    if (pipe_ovf())                                /* 総量 4KB 超で打ち切った(z80 の user/sh.c と同じ文言) */
        kprintf("\nsh: pipe: truncated at 4KB\n");
    pipe_teardown();
}

/* 外部コマンド(FAT 上の <cmd>.BIN)を起動。
 *   route : 起動プロセスの出力ルート(ROUTE_CONSOLE / ROUTE_DISCARD)
 *   bg    : 0=フォアグラウンド(子ブロック解放まで spin 待ち) / 1=背景(即戻り)
 * 戻り: 起動したら 1、ファイル無しなら 0。 */
static int exec_external(const char *cmd, const char *arg, unsigned char route, int bg)
{
    unsigned char n;

    n = launch(cmd, arg);
    if (n == 0xFF)
        return 0;                        /* ファイル無し -> sh が not found 表示 */
    if (n == 0) {
        kprintf("sh: %s: no free block\n", cmd);
        return 1;
    }

    /* kexec_file は out_route[n]=CONSOLE で初期化済み。/dev/null 等なら上書き。
     * pid_tbl[n]=n の直後なので、ごく短い間 CONSOLE 既定で走る可能性がある
     * (数命令の窓、100Hz tick との競合で稀に数バイト漏れる程度。許容)。 */
    if (route != ROUTE_CONSOLE)
        OUTROUTE[n] = route;

    if (!bg) {
        while (PIDTBL[n] != 0) {          /* foreground: wait child exit */
            if (con_break()) {            /* Ctrl+C → 前景プロセスを kill */
                PIDTBL[n] = 0;            /* 単一バイト store は atomic(cmd_kill と同じ) */
                kputchar('\n');           /* 子の出力途中から行を分ける */
                kputchar('^');            /* Linux 風に ^C をエコー */
                kputchar('C');
                kputchar('\n');
            }
        }
    }
    return 1;                            /* background: return immediately */
}

static void split_cmd_arg(char *str, char **cmd, char **arg)
{
    char *p = str;
    char *e;
    while (*p == ' ') p++;
    *cmd = p;
    while (*p && *p != ' ') p++;
    if (*p) {
        *p++ = 0;
        while (*p == ' ') p++;
    }
    *arg = p;
    /* arg の末尾空白を落とす(`ls /dev | prx` や `echo x > f` で '|'/'>' 前の
     * スペースが arg に残り、パス末尾に空白が付くのを防ぐ)。 */
    for (e = p; *e; e++)
        ;
    while (e > p && e[-1] == ' ')
        *--e = 0;
}


int sh_execute(char *line)
{
    char *p, *cmd, *arg, *redir, *inred, *pipe_rhs;
    int  r, bg, discard;
    int  redir_app;

    /* 末尾 "&" を除去 */
    bg = 0;
    for (p = line; *p; p++);
    while (p > line && p[-1] == ' ') *--p = 0;
    if (p > line && p[-1] == '&') {
        *--p = 0; bg = 1;
        while (p > line && p[-1] == ' ') *--p = 0;
    }

    /* パイプ "|" */
    pipe_rhs = 0;
    for (p = line; *p; p++) {
        if (*p == '|') {
            *p++ = 0; while (*p == ' ') p++;
            pipe_rhs = p; break;
        }
    }

    if (pipe_rhs && *pipe_rhs) {
        char *cmd2, *arg2;
        split_cmd_arg(line, &cmd, &arg);
        split_cmd_arg(pipe_rhs, &cmd2, &arg2);
        if (*cmd == 0 || *cmd2 == 0) return 0;
        if (cwd[1] != 0) {
            char *ra = resolve_arg(cmd, arg);
            if (ra != arg) { unsigned char i; for (i = 0; ra[i] && i < CWD_MAX - 1; i++) pipebuf[i] = ra[i]; pipebuf[i] = 0; arg = pipebuf; }
            arg2 = resolve_arg(cmd2, arg2);
        }
        if (!builtin_is(cmd) && !builtin_is(cmd2)) { run_kpipe(cmd, arg, cmd2, arg2); return 0; }
        if (redir_begin("tmp.pip", 0) == 0) {
            r = builtin_try(cmd, arg); redir_end();
            if (r == BUILTIN_NONE) { kfs_unlink("tmp.pip"); return 0; }
        } else return 0;
        if (in_begin("tmp.pip") == 0) {
            r = builtin_try(cmd2, arg2);
            if (r == BUILTIN_NONE) { if (!exec_external(cmd2, arg2, ROUTE_CONSOLE, 0)) kprintf("sh: %s: not found\n", cmd2); }
            in_end();
        }
        kfs_unlink("tmp.pip"); return 0;
    }

    /* リダイレクト/パイプ解析 */
    redir = 0; redir_app = 0; inred = 0;
    for (p = line; *p; p++) {
        if (*p == '>' || *p == '<') {
            if (*p == '>') { redir = ++p; if (*p == '>') { redir_app = 1; redir++; } while (*redir == ' ') redir++; }
            else { inred = ++p; while (*inred == ' ') inred++; }
            p[-1] = 0;
            if (redir) { p = redir; while (*p && *p != ' ') p++; *p = 0; break; }
            if (inred) { p = inred; while (*p && *p != ' ') p++; *p = 0; break; }
        }
    }

    split_cmd_arg(line, &cmd, &arg);
    if (*cmd == 0) return 0;
    if (strcmp(cmd, "pwd") == 0) { kprintf("%s\n", cwd); return 0; }
    if (strcmp(cmd, "cd")  == 0) { do_cd(arg); return 0; }

    if (cwd[1] != 0) {
        arg = resolve_arg(cmd, arg);
        if (redir && *redir && *redir != '/' && path_norm(cwd, redir, redirbuf) == 0) redir = redirbuf;
        else if (inred && *inred && *inred != '/' && path_norm(cwd, inred, redirbuf) == 0) inred = redirbuf;
    }

    discard = 0;
    if (redir && *redir) {
        int idx = vfs_lookup(redir);
        struct vnode *vn = vfs_node(idx);
        if (vn && vn->type == VT_DEV_NULL) discard = 1;
    }

    if (redir && *redir && discard) {
        OUTROUTE[0] = ROUTE_DISCARD;
        r = builtin_try(cmd, arg);
        OUTROUTE[0] = ROUTE_CONSOLE;
        if (r == BUILTIN_NONE) { if (!exec_external(cmd, arg, ROUTE_DISCARD, bg)) kprintf("sh: %s: not found\n", cmd); }
        else if (r == BUILTIN_EXIT) return 1;
        return 0;
    }

    if (redir && *redir) {
        if (redir_begin(redir, (unsigned char)redir_app) == 0) {
            r = builtin_try(cmd, arg);
            if (r == BUILTIN_NONE) { if (!exec_external(cmd, arg, ROUTE_CONSOLE, 0)) kprintf("sh: %s: not found\n", cmd); }
            redir_end();
        }
        return 0;
    }

    if (inred && *inred) {
        if (in_begin(inred) == 0) {
            r = builtin_try(cmd, arg);
            if (r == BUILTIN_NONE) { if (!exec_external(cmd, arg, ROUTE_CONSOLE, 0)) kprintf("sh: %s: not found\n", cmd); }
            in_end();
        }
        return 0;
    }

    r = builtin_try(cmd, arg);
    if (r == BUILTIN_NONE) { if (!exec_external(cmd, arg, ROUTE_CONSOLE, bg)) kprintf("sh: %s: not found\n", cmd); }
    else if (r == BUILTIN_EXIT) return 1;
    return 0;
}



void sh(void)
{

    /* /etc/rc: 起動時に一度だけ実行する。無ければ何もしない。
     *   user/sh.c の run_rc() と同じ意味論(空行と '#' 始まりは無視、
     *   1 行 LINE_MAX-1 文字まで)を、カーネル側の入力リダイレクト
     *   (in_begin/in_src/in_end)で実装したもの ── カーネルに FILE* は無い。
     *   sh_execute() は自前で in_* を張り直すことがあるので、rc の読み出しは
     *   1 行ずつ完結させず「全行を読んでから実行」ではなく、
     *   in_end() してから実行する形にはできない(順序が要る)。ここでは
     *   in_src() で 1 行取り出したら即 sh_execute() する ── sh_execute() が
     *   リダイレクトを使う行では in_* が入れ子になるため、rc に "<" や ">" を
     *   含む行は書かないこと。 */
    if (in_begin("/etc/rc") == 0) {
        int c;
        unsigned char n = 0;

        for (;;) {
            c = in_src();
            if (c == -1 || c == '\n' || c == '\r') {
                line[n] = 0;
                if (n > 0 && line[0] != '#')
                    sh_execute(line);
                n = 0;
                if (c == -1) break;
            } else if (n < LINE_MAX - 1) {
                line[n++] = (unsigned char)c;
            }
        }
        in_end();
    }

    cwd[0] = '/'; cwd[1] = 'r'; cwd[2] = 'o'; cwd[3] = 'o'; cwd[4] = 't';
    cwd[5] = 0;
    for (;;) {
        {
            char *q = prompt;
            const char *c = cwd;
            *q++ = '[';
            while (*c) *q++ = *c++;
            *q++ = ']';
            *q++ = '#';
            *q++ = ' ';
            *q = 0;
        }
        if (readline(prompt, line, LINE_MAX) < 0) {
            kputchar('\n');
            kprintf("shutdown\n");
            return;
        }
#if defined(ARCH_X86_IA16) || defined(ARCH_M68K_MEGA)
        {
            /* ヒストリは実行の**後**に足す(z80 の user/sh.c と同じ順序)。
             * 先に足すと `rm /root/history` で自分の行ごと消え、`history`
             * が自分自身を末尾に表示する。sh_execute は line を切り刻むので写す。 */
            char hline[LINE_MAX];
            strcpy(hline, line);
            sh_execute(line);
            hist_add(hline);
        }
#else
        sh_execute(line);
#endif
    }
}
