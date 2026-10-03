/* user/sh.c - tizix シェル(外部コマンド版 /bin/sh.bin)
 *
 *   z80 では sh はカーネル像から外れ、init(PID 1)が起動・respawn する
 *   外部コマンド。crt0cmd 規約(iy_reg PIC・掟)に従う:
 *     - 書き込み可能な file-scope static を持てない(iy_reg 未変換 +
 *       gsinit 非実行)。セッション状態は絶対番地固定の struct へ置く
 *       (SH_STATE = kmem.h KW_SHSTATE)。数値番地なので iy_reg 素通し。
 *     - 大小比較は unsigned 一発(符号付き int の < > は禁止)。
 *     - 除算/乗算/剰余禁止。
 *
 *   カーネルとの接点:
 *     stdio.h  … putchar/getchar/printf/puts/opendir/readdir/closedir/
 *                unlink/kbhit(drv_tbl[0..25])
 *     shvec.h  … kexec_argv, builtin_try, builtin_is, redir/in begin+end,
 *                pipe_setup ほか(drv_tbl[27..37])
 *     0x8400 pid_tbl / 0x851A out_route を直接 poke(数値番地)。
 *
 *   引数受け渡し: sh は行をトークン化(クォート対応)して NUL 区切りの argpack を
 *   作り、kexec_argv(fname, argpack, argc) で渡すだけ。**トークンの中身は一切
 *   解釈しない。**
 *
 *   #28 以前は sh がここで「どの引数がパスか」を推測して cwd 起点に絶対化して
 *   いた(is_path_cmd の 16 個ハードコード / 直前が "-n" なら次は数値 /
 *   '=' を含むなら key=value / grep の第 1 引数は PATTERN…)。`wc -l` が
 *   "cannot open -l" になり、コマンドにオプションを足すたび sh が壊れる原因
 *   だった。cwd はカーネルが持ち(src/fatcmd.c)、パスを受け取るカーネル入口
 *   (drv_open / kdir_open / kfs_* / redir_begin / in_begin)が解決する。
 *   sh は cd と プロンプト表示のために kchdir/kgetcwd を叩くだけ。
 */
#include "stdio.h"
#include "string.h"
#include "shvec.h"

#define LINE_MAX   48
#define CWD_MAX    48
/* プロセス表・出力ルート・セッション状態の置き場所はアーキが決める。
 *   z80: 下の既定(数値番地を直接読み書き。iy_reg 素通し)。
 *   gcc 系(m68k-mega / esp32-wroom-32e): shvec.h の gcc 側が syscall 版と static 領域で
 *   先に定義する(カーネルの kwork 番地はリンク時にしか決まらないため)。 */
#ifndef SH_STATE
#define SH_STATE   0x8700           /* kmem.h KW_SHSTATE と一致必須。DRIVER fd_table
                                    * (0x8600-0x86F0)より上。 */
#endif

#ifndef SH_PID
#define PIDTBL    ((volatile unsigned char *)0x8400)   /* pid_tbl[block] 0=free */
#define OUTROUTE  ((volatile unsigned char *)0x851A)   /* out_route[block]      */
#define SH_PID(n)          (PIDTBL[n])                 /* 0 = 終了済み      */
#define SH_KILL(n)         (PIDTBL[n] = 0)
#define SH_ROUTE(n, r)     (OUTROUTE[n] = (r))
#endif
#define ROUTE_CONSOLE  0
#define ROUTE_DISCARD  1
#define ROUTE_PIPE     2

#define BUILTIN_NONE   0
#define BUILTIN_OK     1
#define BUILTIN_EXIT   2

/* 絶対番地固定のセッション状態(file-scope static の代わり)。
 *   作業領域は自スタックに置かず、ここ(block0 の空き帯)へ逃がす。
 *   #28 で scr1(絶対化結果)は不要になった。cwd はカーネルが持ち、ここの
 *   cwd[] はプロンプト表示用の写し(kgetcwd で毎回引き直す)。 */
struct sh_state {
    char cwd[CWD_MAX];                 /* プロンプト用の写し。真の cwd はカーネル */
    char line[LINE_MAX];               /* 行編集バッファ                  */
    char prompt[CWD_MAX + 8];          /* "[" + cwd + "]# "               */
    char redirbuf[CWD_MAX];            /* > / < のファイル名(絶対化はしない) */
    char scr1[CWD_MAX];                /* 予約(旧: 絶対化結果)。kmem.h の枠は据置 */
    char scr2[CWD_MAX];                /* build_pack: 生トークン          */
    char argpack[144];                /* 右辺/主コマンドの NUL 区切り引数 */
    char argpackL[96];                /* パイプ左辺の NUL 区切り引数      */
    unsigned char nargp;              /* argpack のトークン数             */
    unsigned char nargpL;            /* argpackL のトークン数           */
    unsigned char hhead;              /* #45 ヒストリ: 次に書くスロット    */
    unsigned char hcnt;               /*       有効件数(hhead の直後必須)  */
};
#ifdef SH_STATE_SIZE                  /* アーキ側が領域を確保した場合の大きさ検査 */
typedef char sh_state_fits[(sizeof(struct sh_state) <= SH_STATE_SIZE) ? 1 : -1];
#endif

/* ================================================================== */
/* 端末入出力                                                          */
/* ================================================================== */

/* Ctrl+C が来ていれば 1。
 * #33: 自前の kbhit()+getchar() をやめ、カーネルの con_break を呼ぶ。
 * 旧実装は **Ctrl+C 以外の 1 バイトを読んで捨てて**いたので、端末から読む
 * 前景コマンド(対話 cat / vi)と 1 バイト単位で取り合ってキーが消えていた。
 * カーネル版は Ctrl+C 以外を戻しバッファへ退避し、前景側の kgetchar が拾う。 */
static unsigned char sh_break(void)
{
    return (unsigned char)(kcon_break() != 0);
}

/* ================================================================== */
/* ヒストリ(#45)                                                      */
/* ================================================================== */
/* /root/history に固定長スロットのリングで持つ。ファイル内容は動かさない。
 *   [0]                 hhead: 次に書くスロット番号 (0..HIST_N-1)
 *   [1]                 hcnt : 有効件数 (0..HIST_N)
 *   [2 + k*LINE_MAX]    スロット k: 行バッファそのまま(NUL 終端、後ろはゴミ)
 * 100 件を超えると最古のスロットを上書きする(ローテーション)。最大 4802B。
 *
 * 1 回の操作は fopen → 前方 fseek 1 回 → read/write → fclose で完結させる。
 * 巻き戻し seek は使わない(外部コマンドの掟: EOF 後の SEEK_SET が返らない
 * 実績あり)。ヘッダを先頭に置いて「ヘッダ → スロット」の順に書けば前方だけ。
 * RAM に持つのは hhead/hcnt の写し 2B だけ(SH_STATE の残りに収まる)。 */
#define HIST_FILE  "/root/history"
#define HIST_N     100
#define HIST_HDR   2

#if LINE_MAX != 48
#error "hist_off() は LINE_MAX=48 をシフトで展開している"
#endif

/* スロット k のファイル内オフセット = 2 + k*48(乗算ヘルパを引かないようシフトで) */
static long hist_off(unsigned char k)
{
    return (long)(HIST_HDR + ((unsigned)k << 5) + ((unsigned)k << 4));
}

static void hist_init(struct sh_state *S)
{
    FILE *f = fopen(HIST_FILE, "r");

    S->hhead = 0;
    S->hcnt = 0;
    if (f) {
        fread(&S->hhead, 1, 2, f);           /* hhead, hcnt は struct で隣接 */
        fclose(f);
    }
    if (S->hhead >= HIST_N || S->hcnt > HIST_N) {   /* 壊れていたら空から */
        S->hhead = 0;
        S->hcnt = 0;
    }
}

/* 入力した行 line を末尾に 1 件足す。空行は積まない。
 *   line は S->line の写し(パースが S->line を in-place で切り刻むため)。
 *   SD への書き込み(読み書き 5 回ほど)がコマンドの出力を待たせないよう、
 *   呼ぶのはコマンド実行の**後**(main ループ参照)。 */
static void hist_add(struct sh_state *S, const char *line)
{
    FILE *f;
    unsigned char k = S->hhead;

    if (line[0] == 0) return;
    f = fopen(HIST_FILE, "r+");
    if (!f) {                                /* 無い(初回 / rm された)→ 作り直し */
        f = fopen(HIST_FILE, "w");
        if (!f) return;
        k = 0;
        S->hcnt = 0;
    }
    S->hhead = (unsigned char)(k + 1u == HIST_N ? 0 : k + 1u);
    if (S->hcnt < HIST_N) S->hcnt++;
    fwrite(&S->hhead, 1, 2, f);
    fseek(f, hist_off(k), SEEK_SET);
    fwrite(line, 1, LINE_MAX, f);
    fclose(f);
}

/* back 個前(1=直前)を buf へ読み、長さを返す。読めなければ 0(空行)。 */
static unsigned hist_get(struct sh_state *S, unsigned char back, char *buf)
{
    FILE *f;
    unsigned char k = S->hhead;

    buf[0] = 0;
    if (k < back) k += HIST_N;
    k -= back;
    f = fopen(HIST_FILE, "r");
    if (!f) return 0;
    fseek(f, hist_off(k), SEEK_SET);
    fread(buf, 1, LINE_MAX, f);
    fclose(f);
    buf[LINE_MAX - 1] = 0;
    return (unsigned)strlen(buf);
}

/* 画面上の入力を n 文字ぶん消す(BS と ↑↓ の差し替えで共用) */
static void rubout(unsigned n)
{
    while (n) { n--; putchar('\b'); putchar(' '); putchar('\b'); }
}

/* カーソルを画面上・buf 内とも末尾(位置 n)へ揃える(未エコー分を出力するだけ、
 *   buf 自体は変えない)。↑↓でヒストリへ切り替える前や、Enter/^C で確定する
 *   前に呼ぶ ── rubout(n) や改行は「カーソルが末尾にある」前提のため。 */
static void goto_end(const char *buf, unsigned *cur, unsigned n)
{
    while (*cur < n) { putchar(buf[*cur]); (*cur)++; }
}

/* buf[cur] を 1 文字消して詰め、cur..新 n の残りを再描画してカーソルを cur の
 * 位置へ戻す(消した分の空白 1 つを含めて後退)。BS(呼ぶ前に cur を 1 減らし、
 * 画面カーソルも '\b' で 1 つ戻しておく)と Del(cur はそのまま)の共通部分。 */
static void del_at_cursor(char *buf, unsigned cur, unsigned *np)
{
    unsigned n = *np;
    unsigned i;

    for (i = cur; i + 1u < n; i++) buf[i] = buf[i + 1u];
    n--;
    for (i = cur; i < n; i++) putchar(buf[i]);
    putchar(' ');
    for (i = n + 1u; i > cur; i--) putchar('\b');
    *np = n;
}

/* 行編集。戻り: 文字数 / 0xFFFF = EOF(空行で ^D)。
 *   ↑(ESC [ A)で 1 個前、↓(ESC [ B)で 1 個後のヒストリを行へ呼び出す。
 *   呼び出した行は打った行と同じ扱い(BS で削って打ち足せる)。ヒストリ側は
 *   書き換えない ── 実行すれば末尾に 1 件追記されるだけ。
 *   ←→(ESC [ D / ESC [ C)でカーソルを行内移動し、その位置に対して挿入 /
 *   BS 削除する(vi.c の getkey と同じ ESC [ A/B/C/D 規約)。
 *   Del キーは ESC [ 3 ~ で来る。末尾の '~' を読み捨てずに continue すると
 *   次ループの getchar() がそれを拾って行にチルダが挿入されてしまう(実際に
 *   起きていた不具合)ので、'3' が来たら必ず終端の '~' まで読み切ってから
 *   処理する。Insert/Home/End/PgUp/PgDn 等の他の ESC [ <n> ~ 系キーは未対応
 *   (素の ESC 同様に無視され、'~' が行に混じる)。 */
static unsigned sh_readline(struct sh_state *S)
{
    char *buf = S->line;
    unsigned n = 0;                          /* 行の総文字数              */
    unsigned cur = 0;                        /* カーソル位置(0..n)        */
    unsigned c;
    unsigned char back = 0;                  /* 0=新規行 / k=k 個前を表示中 */

    printf("%s", S->prompt);
    for (;;) {
        c = (unsigned)getchar() & 0x7F;
        if (c == 0x1B) {                     /* ESC [ A/B/C/D / <digit> ~ */
            if (getc_timeout(10) != '[') continue;
            c = (unsigned)getc_timeout(10);
            if (c == 'A' || c == 'B') {      /* ↑↓: ヒストリ切り替え */
                if (c == 'A' && back < S->hcnt) back++;
                else if (c == 'B' && back) back--;
                else continue;
                goto_end(buf, &cur, n);
                rubout(n);
                n = 0;
                if (back) { n = hist_get(S, back, buf); printf("%s", buf); }
                cur = n;
            } else if (c == 'C') {           /* → */
                if (cur < n) { putchar(buf[cur]); cur++; }
            } else if (c == 'D') {           /* ← */
                if (cur) { putchar('\b'); cur--; }
            } else if (c == '3') {           /* Del: ESC [ 3 ~ */
                (void)getc_timeout(10);      /* 終端の '~' を読み捨てる */
                if (cur < n) del_at_cursor(buf, cur, &n);
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            goto_end(buf, &cur, n);
            putchar('\n'); buf[n] = 0; return n;
        }
        if (c == 0x04) {                     /* ^D: 空行なら EOF */
            if (n == 0) return 0xFFFFu;
            continue;
        }
        if (c == 0x08 || c == 0x7F) {        /* BS: カーソル直前の 1 文字を削除 */
            if (cur) {
                cur--;
                putchar('\b');
                del_at_cursor(buf, cur, &n);
            }
            continue;
        }
        if (c == 0x03) {                     /* ^C: 行を捨てて空行を返す */
            goto_end(buf, &cur, n);
            putchar('\n'); buf[0] = 0; return 0;
        }
        if (c < 0x20u || c > 0x7Eu) continue;
        /* #32: 行長上限(LINE_MAX)を超えた入力は **黙って捨てない**。
         * 以前は無音で切り捨てていたので、`dd if=/dev/fdb of=X bs=512
         * count=1 skip=1`(ちょうど 48 文字)が末尾 1 文字を失って
         * `skip=` になり、dd が意味不明な引数エラーを出した ── 原因が
         * シェルの行長だと気付くのに時間がかかる類の壊れ方。
         * 入らない文字はベルで知らせる(画面は崩さない)。 */
        if (n + 1u >= LINE_MAX) { putchar('\a'); continue; }
        if (cur == n) {                      /* 末尾への追記(従来どおり) */
            buf[n] = (char)c; n++; cur++; putchar((char)c);
        } else {                             /* カーソル位置への挿入 */
            unsigned i;
            for (i = n; i > cur; i--) buf[i] = buf[i - 1u];
            buf[cur] = (char)c;
            n++;
            for (i = cur; i < n; i++) putchar(buf[i]);
            for (i = n; i > cur + 1u; i--) putchar('\b');
            cur++;
        }
    }
}

/* ================================================================== */
/* cd / pwd                                                            */
/* ================================================================== */
/* cd は kchdir に委譲する(正規化・実在確認・cwd 更新はカーネル側)。
 *   無引数 cd はカーネルがホーム /root として扱う。
 *   ファイル・存在しないパス・パス長超過はまとめて "no such dir"。 */
static void do_cd(struct sh_state *S, const char *arg)
{
    if (kchdir(arg) != 0)
        printf("cd: %s: no such dir\n", arg);
    kgetcwd(S->cwd);                     /* プロンプト用の写しを更新 */
}

/* ================================================================== */
/* トークナイズ + argpack 構築                                         */
/* ================================================================== */

/* arg(非破壊)を空白/クォートで分解し dst へ NUL 区切りで詰める。
 *   "..." / '...' は中身を 1 トークン(クォート除去、内部の空白は保持)。
 *   閉じていなければ -1(呼び出し側がコマンド行を捨てる)。
 *   *nt にトークン数。戻り 0=ok / -1=クォート未閉じ / -2=バッファ溢れ。
 *
 *   #28: トークンの中身は一切見ない。パスかオプションかの判別も、cwd 起点の
 *   絶対化もしない(カーネル入口が kpath で解決する)。旧版の resolve /
 *   skip_first / is_kv / prev_optarg は全て撤去した。 */
static int build_pack(struct sh_state *S, const char *arg,
                      char *dst, unsigned dstsz, unsigned char *nt)
{
    const char *p = arg;
    char *tok = S->scr2;                 /* 生トークン(スタックに置かない) */
    unsigned di = 0;
    unsigned char cnt = 0;

    for (;;) {
        char q = 0;
        unsigned char closed;
        unsigned char tl = 0;

        while (*p == ' ') p++;
        if (!*p) break;

        if (*p == '"' || *p == '\'') { q = *p; p++; closed = 0; }
        else closed = 1;

        while (*p) {
            if (q) {
                if (*p == q) { p++; closed = 1; break; }
            } else if (*p == ' ') {
                break;
            }
            if (tl + 1u < CWD_MAX) tok[tl++] = *p;
            p++;
        }
        tok[tl] = 0;
        if (!closed) return -1;

        {
            unsigned char k;
            for (k = 0; tok[k]; k++) {    /* トークンをそのまま積む(解釈しない) */
                if (di + 2u >= dstsz) return -2;
                dst[di++] = tok[k];
            }
            dst[di++] = 0;               /* トークン終端 */
            cnt++;
        }
    }
    dst[di] = 0;                          /* 末尾二重 NUL(保険) */
    *nt = cnt;
    return 0;
}

/* ================================================================== */
/* 起動 / 前景待ち                                                     */
/* ================================================================== */
#define say_ctrlc()  printf("\n^C\n")

/* コマンド名 → "/bin/<cmd>.bin"(先頭が '/' ならそのまま + ".bin")。fname は BIN_PATH_MAX。
 * z80(FAT が 8.3)は '-' をディレクトリの区切りに読む(esp32-gpio → /bin/esp32/gpio.bin。#112)。
 * gcc 側は長いファイル名が使えるので名前のまま(/bin/esp32-gpio.bin。#114)。
 * src/pipe.c の pipe_fname と同じ規則。 */
#ifdef TZ_SYSCALL
#define BIN_PATH_MAX 48
#else
#define BIN_PATH_MAX 24
#endif
static void bin_path(const char *cmd, char *fname)
{
    unsigned char i, j;
    char c;

    j = 0;
    if (cmd[0] != '/') {
        fname[j++] = '/'; fname[j++] = 'b'; fname[j++] = 'i';
        fname[j++] = 'n'; fname[j++] = '/';
    }
    for (i = 0; cmd[i] && j < BIN_PATH_MAX - 6; i++) {
        c = cmd[i];
#ifndef TZ_SYSCALL
        if (c == '-' && cmd[0] != '/')
            c = '/';
#endif
        fname[j++] = c;
    }
    fname[j++] = '.'; fname[j++] = 'b'; fname[j++] = 'i'; fname[j++] = 'n';
    fname[j] = 0;
}

static unsigned char launch(const char *cmd, const char *argpack, unsigned char argc)
{
    char fname[BIN_PATH_MAX];

    bin_path(cmd, fname);
    return kexec_argv(fname, argpack, argc);
}

/* コマンドのファイルが無ければ 1(パイプの起動失敗を "not found" と "out of memory" に分けるため)。 */
static unsigned char cmd_missing(const char *cmd)
{
    char fname[BIN_PATH_MAX];
    FILE *f;

    bin_path(cmd, fname);
    f = fopen(fname, "r");
    if (!f) return 1;
    fclose(f);
    return 0;
}

/* 戻り: 1=起動した / 0=ファイル無し(sh が not found 表示) */
static int exec_external(const char *cmd, const char *argpack, unsigned char argc,
                         unsigned char route, unsigned char bg)
{
    unsigned char n = launch(cmd, argpack, argc);

    if (n == 0xFF) return 0;                                       /* ファイル無し */
    /* #33: 空きブロック不足を "not found" と言わない。存在しないファイルを
     * 探しに行かせてしまう。kexec_argv の 0 は「nblk 連続の空きが無い /
     * サイズ過大」であって、ファイルは開けている。カーネル側 sh(src/sh.c)は
     * 元からこう出していた ── 外部 sh 化のときに握り潰されていた。
     * 4 ブロック要求の vi は、背景ジョブが 1 個居るだけでここに来る。 */
    if (n == 0) { printf("sh: %s: no free block\n", cmd); return 1; }

    if (route != ROUTE_CONSOLE)
        SH_ROUTE(n, route);

    if (!bg) {
        while (SH_PID(n) != 0) {
            if (sh_break()) { SH_KILL(n); say_ctrlc(); }
        }
        con_raw(0);     /* 前景ジョブが生モード(rx 等)のまま終わっても端末を戻す */
    }
    return 1;
}

/* A | B(両側外部)をカーネルパイプで繋ぐ。#27: 起動・結線・監視は
 *   カーネル(krun_pipe)へ移設。sh は結果コードを見てメッセージを出すだけ。
 *   戻り 1=out of memory / 2=ovf(4KB 超で打ち切り) / 3=Ctrl+C。 */
static void run_kpipe(const char *cw, const char *apw, unsigned char apwn,
                      const char *cr, const char *apr, unsigned char aprn)
{
    unsigned char rc = krun_pipe(cw, apw, apwn, cr, apr, aprn);
    /* #93: krun_pipe の 1 は「起動できない」全部(ファイル無しも含む)。以前は一律に
     * "out of memory" と出ていた(`pppecho hi | wc` のような打ち間違いでも)。 */
    if (rc == 1) {
        if (cmd_missing(cw))      printf("sh: %s: not found\n", cw);
        else if (cmd_missing(cr)) printf("sh: %s: not found\n", cr);
        else                      printf("sh: out of memory\n");
    }
    else if (rc == 2) printf("\nsh: pipe: truncated at 4KB\n");   /* 4KB 打ち切り: 出力途中の行を分離。
                                                     * 以前は rc==1 と同じ "out of memory" で原因が分からなかった */
    else if (rc == 3) say_ctrlc();
}

/* ================================================================== */
/* split: str を cmd と arg(残り一本)に分割(in-place)                  */
/* ================================================================== */
static void split_cmd_arg(char *str, char **cmd, char **arg)
{
    char *p = str;
    char *e;

    while (*p == ' ') p++;
    *cmd = p;
    while (*p && *p != ' ') p++;
    if (*p) { *p++ = 0; while (*p == ' ') p++; }
    *arg = p;
    e = p + strlen(p);
    while (e > p && e[-1] == ' ')
        *--e = 0;
}

/* cmd が "ls" で「パストークン(- で始まらない)」が無ければ cwd を 1 トークン
 * 足す(無引数 ls / `ls -l` は cwd を列挙する、の従来仕様)。
 *
 *   #28 注: これは「どの引数がパスか」の推測ではなく、無引数時の既定値の
 *   付与なので残してある。ls.c 側の既定は "." だが、ls は path が ちょうど "/"
 *   のときだけ /dev を合成表示する ── "." を渡すとルートで dev/ が消える。
 *   本来は kdir_read が "/" の反復で dev を返すべきで(ls が /dev を知っている
 *   のが筋悪)、そこを直せばこの関数ごと消せる。#28 の残タスク。 */
static void ls_default_cwd(const char *cmd, const char *cwd,
                           char *dst, unsigned dstsz, unsigned char *nt)
{
    const char *p = dst;
    unsigned di, cl;
    unsigned char k;

    if (strcmp(cmd, "ls") != 0)
        return;

    for (k = 0; k < *nt; k++) {
        if (*p != '-')
            return;                     /* パス指定あり → 何もしない */
        while (*p) p++;
        p++;
    }

    di = (unsigned)(p - dst);           /* 既存トークン列の直後 */
    cl = (unsigned)strlen(cwd);
    if (di + cl + 2u < dstsz) {
        strcpy(dst + di, cwd);
        dst[di + cl + 1u] = 0;
        (*nt)++;
    }
}

/* build_pack + ls 既定 cwd 付与をまとめる(パイプ左右 + 非パイプの 3 箇所)。
 *   戻り 0=ok / -1=引数不正。 */
static int prep_side(struct sh_state *S, const char *cmd, const char *arg,
                     char *pack, unsigned sz, unsigned char *np)
{
    if (build_pack(S, arg, pack, sz, np) != 0)
        return -1;
    ls_default_cwd(cmd, S->cwd, pack, sz, np);
    return 0;
}

/* builtin を試し、無ければ外部起動(4 つのディスパッチ枝で共用)。
 *   戻り: builtin_try の結果。BUILTIN_EXIT は呼び出し側が return 0 する。 */
static unsigned char run_one(const char *cmd, const char *arg,
                             const char *pack, unsigned char np,
                             unsigned char route, unsigned char bg)
{
    unsigned char r = (unsigned char)builtin_try(cmd, arg);
    if (r == BUILTIN_NONE) {
        if (!exec_external(cmd, pack, np, route, bg))
            printf("sh: %s: not found\n", cmd);
    }
    return r;
}

/* ================================================================== */
/* 1 行分の実行(対話プロンプトと /etc/rc の両方から呼ぶ)                 */
/*   S->line を破壊的にパースして起動する。戻り値は builtin_try 系の結果  */
/*   (BUILTIN_EXIT なら呼び出し側はシェルを終了させること)。               */
/* ================================================================== */
static unsigned char dispatch_line(struct sh_state *S)
{
    char *p, *cmd, *arg, *rd, *pipe_rhs;
    unsigned char bg, discard, r, redir_app, rin;

    /* 末尾 "&" を剥がす(背景実行) */
    bg = 0;
    p = S->line + strlen(S->line);
    while (p > S->line && p[-1] == ' ') *--p = 0;
    if (p > S->line && p[-1] == '&') {
        *--p = 0;
        bg = 1;
        while (p > S->line && p[-1] == ' ') *--p = 0;
    }

    /* パイプ "|" 検出(最初の 1 個) */
    pipe_rhs = strchr(S->line, '|');
    if (pipe_rhs) {
        *pipe_rhs++ = 0;
        while (*pipe_rhs == ' ') pipe_rhs++;
    }

    if (pipe_rhs && *pipe_rhs) {
        char *cmd2, *arg2;

        split_cmd_arg(S->line, &cmd, &arg);
        split_cmd_arg(pipe_rhs, &cmd2, &arg2);
        if (*cmd == 0 || *cmd2 == 0) return BUILTIN_NONE;

        if (prep_side(S, cmd, arg, S->argpackL, sizeof S->argpackL, &S->nargpL) != 0
            || prep_side(S, cmd2, arg2, S->argpack, sizeof S->argpack, &S->nargp) != 0) {
            printf("sh: bad args\n");
            return BUILTIN_NONE;
        }

        /* 両側とも外部 → カーネルパイプ。片側でも builtin なら一時ファイル。 */
        if (!builtin_is(cmd) && !builtin_is(cmd2)) {
            run_kpipe(cmd, S->argpackL, S->nargpL, cmd2, S->argpack, S->nargp);
            return BUILTIN_NONE;
        }

        /* 片側 builtin: tmp.pip 経由(従来方式)。左辺 → tmp.pip → 右辺。 */
        if (redir_begin("tmp.pip", 0) != 0) return BUILTIN_NONE;
        run_one(cmd, arg, S->argpackL, S->nargpL, ROUTE_CONSOLE, 0);
        redir_end();
        if (in_begin("tmp.pip") == 0) {
            run_one(cmd2, arg2, S->argpack, S->nargp, ROUTE_CONSOLE, 0);
            in_end();
        }
        unlink("tmp.pip");
        return BUILTIN_NONE;
    }

    /* リダイレクト: 最初の '>'(">>" 追記)か '<'。混在は先勝ち・排他。 */
    rd = 0;
    rin = 0;
    redir_app = 0;
    for (p = S->line; *p; p++) {
        if (*p != '>' && *p != '<') continue;
        rin = (unsigned char)(*p == '<');
        *p++ = 0;
        if (!rin && *p == '>') { redir_app = 1; p++; }
        while (*p == ' ') p++;
        rd = p;
        while (*p && *p != ' ') p++;
        *p = 0;
        break;
    }

    split_cmd_arg(S->line, &cmd, &arg);
    if (*cmd == 0) return BUILTIN_NONE;

    if (strcmp(cmd, "pwd") == 0) { puts(S->cwd); return BUILTIN_NONE; }
    if (strcmp(cmd, "cd")  == 0) { do_cd(S, arg); return BUILTIN_NONE; }

    /* #27: `tail < file` は tail が stdin スプール経路(スタック不足)に落ちる。
     *   `tail file`(argv 渡し)へ書き換える。tail 以外の `< file` は従来どおり。 */
    if (rin && rd && *rd && !*arg && strcmp(cmd, "tail") == 0) {
        arg = rd;
        rd = 0;
        rin = 0;
    }

    /* トークナイズ → argpack(中身は解釈しない) */
    if (prep_side(S, cmd, arg, S->argpack, sizeof S->argpack, &S->nargp) != 0) {
        printf("sh: bad args\n");
        return BUILTIN_NONE;
    }

    /* リダイレクト先の絶対化はしない。redir_begin / in_begin(カーネル)が
     * kpath で cwd 起点に解決する。 */

    discard = (unsigned char)(rd && *rd && !rin && strcmp(rd, "/dev/null") == 0);

    /* ---- ディスパッチ(枝は排他。r に builtin_try 結果を集約)---- */
    if (discard) {
        SH_ROUTE(0, ROUTE_DISCARD);
        r = run_one(cmd, arg, S->argpack, S->nargp, ROUTE_DISCARD, bg);
        SH_ROUTE(0, ROUTE_CONSOLE);
    } else if (rd && *rd && !rin) {
        if (redir_begin(rd, redir_app) != 0) return BUILTIN_NONE;
        r = run_one(cmd, arg, S->argpack, S->nargp, ROUTE_CONSOLE, 0);
        redir_end();
    } else if (rd && *rd) {
        if (in_begin(rd) != 0) return BUILTIN_NONE;
        r = run_one(cmd, arg, S->argpack, S->nargp, ROUTE_CONSOLE, 0);
        in_end();
    } else {
        r = run_one(cmd, arg, S->argpack, S->nargp, ROUTE_CONSOLE, bg);
    }
    return r;
}

/* ================================================================== */
/* /etc/rc: sh 起動時に一度だけ実行する起動スクリプト                    */
/*   無ければ何もしない。1 行 LINE_MAX-1(47)文字まで(fgets はそれ以上を   */
/*   次の呼び出しへ持ち越すので、長い行を置くと以降の行がずれる)。         */
/*   "#" 始まりはコメントとして無視する。                                */
/* ================================================================== */
static void run_rc(struct sh_state *S)
{
    FILE *f = fopen("/etc/rc", "r");
    unsigned char n;

    if (!f) return;
    for (;;) {
        if (fgets(S->line, LINE_MAX, f) == 0) break;
        n = (unsigned char)strlen(S->line);
        while (n > 0 && (S->line[n - 1] == '\n' || S->line[n - 1] == '\r'))
            S->line[--n] = 0;
        if (n == 0 || S->line[0] == '#') continue;
        if (dispatch_line(S) == BUILTIN_EXIT) break;   /* rc 内の exit は rc を打ち切るだけ */
    }
    fclose(f);
}

/* ================================================================== */
/* メインループ                                                        */
/* ================================================================== */
int main(int argc, char **argv)
{
    struct sh_state *S = (struct sh_state *)SH_STATE;

    (void)argc; (void)argv;

    /* z80board 実機プローブ(a〜e、2026-09-19 のブリングアップで使用):
     * 直接ポート出力。DRIVER/カーネルの出力経路を通らないので、printf 側が
     * 壊れていても main がどこまで進んだか見える。再度使う時は SH_PROBE を 1 に
     * (crt0sh.s の S/T/U も合わせて ; を外す)。push af/pop af は必須 ──
     * 囲まないと SDCC が A の生存値を知らずに壊し、hist_init から戻れなくなる。 */
#define SH_PROBE 0
#if SH_PROBE
    __asm
        push    af
        ld      a, #0x61
        out     (0x01), a
        pop     af
    __endasm;
#endif
    /* cwd の実体はカーネル(fat_init が "/root" で初期化済み)。ここは
     * プロンプト表示用の写しを引くだけ。 */
    kgetcwd(S->cwd);
#if SH_PROBE
    __asm
        push    af
        ld      a, #0x62
        out     (0x01), a
        pop     af
    __endasm;
#endif
    hist_init(S);
#if SH_PROBE
    __asm
        push    af
        ld      a, #0x63
        out     (0x01), a
        pop     af
    __endasm;
#endif
    run_rc(S);
#if SH_PROBE
    __asm
        push    af
        ld      a, #0x64
        out     (0x01), a
        pop     af
    __endasm;
#endif

    for (;;) {
        /* プロンプト "[<cwd>]# "(inline ループより strcpy/strcat の方が
         *   iy_reg 展開後は小さい。以下同様に string.rel を活用する)。 */
        S->prompt[0] = '[';
        strcpy(S->prompt + 1, S->cwd);
        strcat(S->prompt, "]# ");
#if SH_PROBE
        __asm
            push    af
            ld      a, #0x65
            out     (0x01), a
            pop     af
        __endasm;
#endif
        if (sh_readline(S) == 0xFFFFu) {
            putchar('\n');
            puts("shutdown");
            return 0;
        }
        {
            /* ヒストリの SD 書き込みはコマンド実行の**後**に回す(先に書くと
             * 読み書き 5 回ほどの待ちがそのまま出力の遅れになる)。パースが
             * S->line を切り刻むので、書く行はここで写しておく。static に
             * しない([[external-cmd-authoring-constraints]]:可変 static 配列を
             * 避ける)── sh のスタックに 48B 積むだけ。 */
            char hline[LINE_MAX];

            strcpy(hline, S->line);
            if (dispatch_line(S) == BUILTIN_EXIT) {
                hist_add(S, hline);      /* exit も履歴に残す(従来どおり) */
                return 0;
            }
            hist_add(S, hline);
        }
    }
}
