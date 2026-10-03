/* pipe.c - カーネルパイプ(#27: 4KB ブロックバッファ版)。
 *
 *   同時 1 本。sh の run_kpipe が A | B を張るとき:
 *     1) pipe_setup(rblk) が **プロセス枠(block2..7)の空きブロックを 1 個確保**し
 *        (pid_tbl[k]=PID_PIPEBUF、sched がスキップ)、そこを線形バッファにする。
 *        空きが無ければ 0 を返す(sh は "out of memory")。
 *     2) pipe_attach_writer(wblk) で writer を結線。
 *   writer の putchar → ROUTE_PIPE → pipe_putc(バッファ末尾へ追記)。
 *   reader の getchar → pipe_is_reader → pipe_getc(先頭から取り出し)。
 *
 *   容量 = 1 ブロック(4096B)。**wrap しない / フロー制御しない**(task #27)。
 *   ストリーム総量が 4096B を超えたら ovf を立て、以後の putc は -1。
 *   sh の待ちループが pipe_ovf() を見てパイプ全体を "out of memory" で中断する。
 *   → 4096B を超える streaming は malloc/realloc 実装後(#27 項目 a, LOW)。
 *
 *   pipe_getwin(&len): reader(tail 等)が writer 完了を待ってから 4KB 窓を
 *   直読みするための入口。base アドレスを返し、*len に確定バイト数(tail)。
 *
 *   krun_pipe(): A | B の結線・監視を **カーネル側** で行う入口(#27 で sh から
 *   移設)。sh は iy_reg 展開でこの規模のロジックを持つと 3 ブロックに収まらず、
 *   かつ「掟」税が重い。カーネル(block0、iy_reg 非適用)なら素直に書ける。
 *   sh の run_kpipe はこれを 1 回呼ぶだけ。
 *
 *   詳細は DEVELOP.md「VFS 一本化」/ task.md #27。 */
#include "kmem.h"
#include "kernel.h"     /* proc_block / proc_wake */
#include "kexec.h"      /* kexec_argv */
#include "io.h"         /* con_break */
#include "pipe.h"

#if defined(ARCH_X86_IA16)
/* ---- x86 パイプは Step 14。ビルドに含めても無害な no-op スタブ ---- */
unsigned char pipe_setup(unsigned char r) __sdcccall(0) { (void)r; return 0; }
void pipe_attach_writer(unsigned char w) __sdcccall(0) { (void)w; }
void pipe_teardown(void) __sdcccall(0) {}
int  pipe_putc(int c) __sdcccall(0) { return c; }
int  pipe_getc(void) __sdcccall(0) { return -1; }
unsigned char pipe_is_reader(unsigned char b) __sdcccall(0) { (void)b; return 0; }
void pipe_note_exit(unsigned char b) __sdcccall(0) { (void)b; }
unsigned char pipe_ovf(void) __sdcccall(0) { return 0; }
unsigned char pipe_tail(unsigned want) __sdcccall(0) { (void)want; return 0; }
unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                        const char *rn, const char *rp, unsigned char rc) __sdcccall(0)
{ (void)ln;(void)lp;(void)lc;(void)rn;(void)rp;(void)rc; return 1; }
#else

#define BUFCAP   0x1000u                 /* 1 ブロック = 4096B。wrap なし */

struct kpipe {
    unsigned char active;
    unsigned char wblk, rblk;
    unsigned char bufblk;               /* 確保したバッファブロック番号(2..7)。0=未確保 */
    unsigned char weof;                 /* writer 終了。reader は残りを吐いて EOF     */
    unsigned char rgone;                /* reader 終了。以後 writer の putc は -1      */
    unsigned char ovf;                  /* 総量 > BUFCAP。パイプは中断される          */
    unsigned int  head, tail;           /* 0..BUFCAP。count = tail - head(wrap 無し)  */
};

#if defined(PLAT_FLAT32)
/* #82: kwork に 32B 取ってある(src/kmem.h KW_PIPE)。超えたら止める。 */
typedef char kpipe_fits[(sizeof(struct kpipe) <= 0x20) ? 1 : -1];
#endif

#define P     ((volatile struct kpipe *)KW_PIPE)
#define PIDT  ((volatile unsigned char *)KW_PIDTAB)
#define PBUF(pp) ((volatile unsigned char *)BLOCK_ADDR((pp)->bufblk))   /* #82: z80 = RAM_BASE + n*4KB / PLAT_FLAT32 = plat.h の PLAT_SLOT_ADDR(n) */

/* reader を launch した直後に呼ぶ。バッファブロックを 1 個確保する。
 *   戻り 1=OK / 0=プロセス枠に空きブロック無し(sh は "out of memory")。 */
unsigned char pipe_setup(unsigned char rblk) __sdcccall(0)
{
    unsigned char k;

    IRQ_OFF();
    P->bufblk = 0;
    for (k = PROC_BLOCK_MIN; k <= PROC_BLOCK_MAX; k++) {
        if (PIDT[k] == PID_FREE) {
            PIDT[k] = PID_PIPEBUF;      /* sched_pick がスキップ / kexec は占有扱い */
            P->bufblk = k;
            break;
        }
    }
    if (P->bufblk == 0) { IRQ_ON(); return 0; }

    P->rblk  = rblk;
    P->wblk  = 0xFF;                    /* placeholder(proc_wake は >=8 を無視) */
    P->weof  = 0;
    P->rgone = 0;
    P->ovf   = 0;
    P->head  = 0;
    P->tail  = 0;
    P->active = 1;
    IRQ_ON();
    return 1;
}

void pipe_attach_writer(unsigned char wblk) __sdcccall(0)
{
    P->wblk = wblk;
}

void pipe_teardown(void) __sdcccall(0)
{
    IRQ_OFF();
    P->active = 0;
    if (P->bufblk) {                    /* 確保したブロックを返す */
        PIDT[P->bufblk] = PID_FREE;
        P->bufblk = 0;
    }
    IRQ_ON();
}

/* reader が既に消えているか(PIDTBL==0)。placeholder(0xFF > PROC_BLOCK_MAX)は「まだ」扱い。 */
static unsigned char reader_gone(void)
{
    return (unsigned char)(P->rgone ||
        (P->rblk <= PROC_BLOCK_MAX && PIDT[P->rblk] == PID_FREE));
}
static unsigned char writer_gone(void)
{
    return (unsigned char)(P->weof ||
        (P->wblk <= PROC_BLOCK_MAX && PIDT[P->wblk] == PID_FREE));
}

/* writer: 1 バイト追記。満杯(総量 4096B 到達)なら ovf を立てて -1。
 *   wrap も flow control もしない(task #27)。reader が消えていたら -1。 */
int pipe_putc(int c) __sdcccall(0)
{
    IRQ_OFF();
    if (reader_gone()) { P->rgone = 1; IRQ_ON(); return -1; }
    if (P->tail < BUFCAP) {
        unsigned int was = P->tail - P->head;
        PBUF(P)[P->tail] = (unsigned char)c;
        P->tail++;
        IRQ_ON();
        if (was == 0)
            proc_wake(P->rblk);        /* 空だった → reader を起こす */
        return c;
    }
    P->ovf = 1;                         /* 4096B 超過 → パイプ中断へ */
    IRQ_ON();
    proc_wake(P->rblk);
    return -1;
}

/* reader: 1 バイト取得。空で writer 継続中なら proc_block。
 *   writer 終了 or ovf で空になったら -1(EOF)。 */
int pipe_getc(void) __sdcccall(0)
{
    int c;

    for (;;) {
        IRQ_OFF();
        if (P->head < P->tail) {
            unsigned int was = P->tail - P->head;
            c = PBUF(P)[P->head];
            P->head++;
            IRQ_ON();
            if (was == BUFCAP)
                proc_wake(P->wblk);    /* 満杯だった(理論上)→ 保険 */
            return c;
        }
        if (P->weof || P->ovf || writer_gone()) { IRQ_ON(); return -1; }
        IRQ_ON();
        proc_wake(P->wblk);
        proc_block();
    }
}

/* pipe_tail: パイプ後段の tail 用。writer 完了まで待って 4KB 窓を後方スキャンし、
 *   末尾 want 行を kputchar で出力する(reader プロセスの文脈で走るので出力は
 *   その端末へ)。tail.c 側は 1 行 `pipe_tail(want)` で済み、1 ブロックに収まる。
 *   戻り 1=パイプ後段として処理した / 0=パイプの reader ではない(tail が
 *   "needs FILE or pipe" を出す)。総量 4KB 超なら先頭 4KB 分の末尾 want 行。 */
unsigned char pipe_tail(unsigned want) __sdcccall(0)
{
    volatile unsigned char *b;
    unsigned n, i, start, end, nl;

    for (;;) {
        IRQ_OFF();
        if (!P->active) { IRQ_ON(); return 0; }
        if (P->weof || P->ovf || writer_gone()) break;
        IRQ_ON();
        proc_wake(P->wblk);
        proc_block();
    }
    n = P->tail;
    b = PBUF(P);
    IRQ_ON();
    if (n == 0) return 1;

    end = n;
    if (b[end - 1] == '\n') end--;          /* 末尾改行は行区切り扱いで勘定外 */
    start = 0;
    nl = 0;
    i = end;
    while (i > 0) {
        i--;
        if (b[i] != '\n') continue;
        nl++;
        if (nl >= want) { start = i + 1; break; }
    }
    for (i = start; i < n; i++)
        kputchar(b[i]);
    return 1;
}

unsigned char pipe_is_reader(unsigned char blk) __sdcccall(0)
{
    return (unsigned char)(P->active && P->rblk == blk);
}

/* 総量が 4096B を超えたか(sh の run_kpipe 待ちループが見る)。 */
unsigned char pipe_ovf(void) __sdcccall(0)
{
    return (unsigned char)(P->active && P->ovf);
}

/* sh の run_kpipe 待ちループが相手の終了(PIDTBL==0)を見て呼ぶ。
 * writer 終了 → weof、reader 終了 → rgone。相手が待っていれば起こす。 */
void pipe_note_exit(unsigned char blk) __sdcccall(0)
{
    if (!P->active) return;
    if (P->wblk == blk && !P->weof) {
        P->weof = 1;
        proc_wake(P->rblk);
    } else if (P->rblk == blk && !P->rgone) {
        P->rgone = 1;
        proc_wake(P->wblk);
    }
}

/* ================================================================== */
/* krun_pipe - A | B の結線・監視(#27 で sh から移設)                  */
/* ================================================================== */

/* "/bin/<name>.bin" を dst[PIPE_FNAME_MAX] へ。name が '/' 始まりならそのまま + ".bin"。
 * z80(FAT が 8.3)はコマンド名の '-' をディレクトリの区切りに読む(esp32-gpio → /bin/esp32/gpio.bin。#112)。
 * gcc 系は長いファイル名が使えるので名前のまま(#114)。user/sh.c の bin_path と同じ規則。 */
#if defined(PLAT_FLAT32)
#define PIPE_FNAME_MAX 48
#else
#define PIPE_FNAME_MAX 24
#endif
static void pipe_fname(char *dst, const char *name)
{
    unsigned char i = 0, j = 0;
    char c;
    if (name[0] != '/') {
        dst[j++] = '/'; dst[j++] = 'b'; dst[j++] = 'i'; dst[j++] = 'n'; dst[j++] = '/';
    }
    while (name[i] && j < PIPE_FNAME_MAX - 6) {
        c = name[i++];
#if !defined(PLAT_FLAT32)
        if (c == '-' && name[0] != '/') c = '/';
#endif
        dst[j++] = c;
    }
    dst[j++] = '.'; dst[j++] = 'b'; dst[j++] = 'i'; dst[j++] = 'n';
    dst[j] = 0;
}

/* プロセス強制終了: 先頭ブロック + 継続ブロック(PID_CONT)を解放する。
 *   crt0.s _kexit と同じ範囲。直接 pid[blk]=0 だけだと 2 ブロック
 *   プロセス(cp / 肥大コマンド)の継続枠がリークする。 */
static void kill_proc(volatile unsigned char *pid, unsigned char blk)
{
    if (blk == 0 || blk > PROC_BLOCK_MAX) return;
    pid[blk] = 0;
    while (++blk <= PROC_BLOCK_MAX && pid[blk] == PID_CONT)
        pid[blk] = 0;
}

/* krun_pipe: writer(左) | reader(右) を起動・結線し、両方終わるまで監視する。
 *   sh の run_kpipe(user/sh.c)が drv_tbl 経由で 1 回呼ぶ。ln/rn は bare
 *   コマンド名、lp/rp は sh が組んだ NUL 区切り argpack、lc/rc は argc。
 *   戻り: 0=正常 / 1=out of memory(起動不可 or 4KB ブロック不足) /
 *         2=ovf(総量 4KB 超で打ち切り) / 3=Ctrl+C 中断。 */
unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                        const char *rn, const char *rp, unsigned char rc) __sdcccall(0)
{
    volatile unsigned char *pid = (volatile unsigned char *)KW_PIDTAB;
    volatile unsigned char *rt  = (volatile unsigned char *)KW_OUTROUTE;
    char fn[PIPE_FNAME_MAX];
    unsigned char wb, rb, ret = 0;

    /* reader 起動 + 4KB バッファブロック確保 */
    pipe_fname(fn, rn);
    rb = kexec_argv(fn, rp, rc);
    if (rb == 0 || rb == 0xFF || !pipe_setup(rb)) {
        if (rb != 0xFF) kill_proc(pid, rb);
        return 1;
    }

    /* writer 起動 */
    pipe_fname(fn, ln);
    wb = kexec_argv(fn, lp, lc);
    if (wb == 0 || wb == 0xFF) {
        pipe_teardown();
        kill_proc(pid, rb);
        return 1;
    }
    pipe_attach_writer(wb);
    rt[wb] = ROUTE_PIPE;

    /* 両方終わるまで監視(sh の文脈で回る。コンテキストスイッチはしない)。 */
    while (pid[wb] != 0 || pid[rb] != 0) {
        if (pid[wb] == 0) pipe_note_exit(wb);
        if (pid[rb] == 0) {
            pipe_note_exit(rb);
            if (pid[wb] != 0) kill_proc(pid, wb);   /* reader 終了 → writer 刈る(ovf 含む) */
        }
        if (con_break()) {
            kill_proc(pid, wb);
            kill_proc(pid, rb);
            ret = 3;
        }
    }
    rt[wb] = ROUTE_CONSOLE;
    if (ret == 0 && pipe_ovf())
        ret = 2;                             /* 総量 4KB 超で静かに打ち切られた */
    pipe_teardown();
    return ret;
}
#endif
