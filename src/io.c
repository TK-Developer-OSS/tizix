#include <stdarg.h>
#include "io.h"
#include "console.h"    /* 層1: con_putc / con_rx_ready / con_getc(arch/<arch>/console.c) */
#include "kmem.h"
#include "pipe.h"       /* カーネルパイプ routing(ROUTE_PIPE / pipe_is_reader) */

/* ==================================================================
 * tizix カーネル I/O (block0)
 *
 *   カーネルの I/O をここに集約(層2: リダイレクト・パイプ・'\n'→CR+LF・
 *   戻しバッファ・行編集)。物理層(層1: コンソールのポートを叩く所)は
 *   arch/<arch>/console.c にあり、ここからは PHYS_* 越しに呼ぶ。
 *   経緯: 当初は cpmsim の port 0/1 をここに直書きし(「con.c への分離は
 *   しない。実機分岐は実機移行段でまとめる」)、arch 分離で con.c を
 *   arch/<arch>/console.c へ移したあとも繋ぎ替えが後回しのままだった。
 *   2026-09-30 に z80pack と z80board を繋いだ(z80board の ROM は、誰も呼ばなく
 *   なっていた readline を src/sh.c へ移して空けた)。
 *
 *   kputchar: 物理層 + redir_on("> file") + CUR_ROUTE(/dev/null) +
 *             '\n'→CR+LF。ブート/ログ/パニックもこれ(ドライバ非依存)。
 *   kgetchar: 物理層 + in_on("< file")。戻り 0..255(符号拡張しない)。
 *   kprintf : 書式変換(kputchar を使う上位)。
 *
 *   ベクタ(crt0.s, ISR 直後 0x003B〜):
 *     0x003B kexit / 0x003E kputchar / 0x0041 kgetchar /
 *     0x0044 getticks / 0x0047 kprintf
 *   ユーザーは user/stdio.h 経由で putchar/printf 等の標準名で叩く
 *   (番地はヘッダが隠す)。ドライバ(block1)も 0x003E を叩く。
 * ================================================================== */

/* ---- 物理層(層1)--------------------------------------------------------
 *   PHYS_PUTC(c)  : 生 1 バイト送出
 *   PHYS_RXRDY()  : 受信 1 バイトあり?(非ブロッキング)
 *   PHYS_GETC()   : 受信 1 バイト取得(RXRDY 済み前提)
 *   実体は arch/<arch>/console.c(約束は console.h)。ポートの番地はここに書かない。
 */
#define PHYS_PUTC(c)  con_putc((char)(c))
#define PHYS_RXRDY()  con_rx_ready()
#define PHYS_GETC()   con_getc()

/* 出力リダイレクト: 関数ポインタは使わない(間接呼び出しが iy を汚し、
 * 外部コマンドの iy=base を壊すため)。フラグ + 直接 call にする。 */
extern int redir_sink(int c);      /* fatcmd.c: ファイルへ 1 バイト書く */
static unsigned char redir_on = 0;
void redir_enable(unsigned char on) { redir_on = on; }

/* per-block 出力ルート表(kmem.h の KW_OUTROUTE)。KW_CURRENT で引く。
 *   out_route[current]==ROUTE_DISCARD なら捨てる(/dev/null)。 */
#define CUR_ROUTE  (((volatile unsigned char *)KW_OUTROUTE)[*(volatile unsigned char *)KW_CURRENT])

/* 入力リダイレクト: 出力側と対称。ここでも関数ポインタは禁止。 */
extern int in_src(void);           /* fatcmd.c: ファイルから 1 バイト。-1=EOF */
static unsigned char in_on = 0;
void in_enable(unsigned char on) { in_on = on; }

/* kputchar: カーネルの 1 バイト出力。物理層を内包。 */
#if defined(ARCH_Z80BOARD)
/* 1 の間はリダイレクト/パイプを無視して物理コンソールへ出す。ディスク
 * ドライバ(arch/z80board/diskio.c)がエラー/ログを出すときに立てる ──
 * `ls > file` 中にディスクの表示がファイル書き込み(f_write)へ流れると
 * FatFs が再入して壊れるため(#54)。 */
unsigned char kcon_direct;
#define KCON_DIRECT kcon_direct
#else
#define KCON_DIRECT 0
#endif

int kputchar(int c)
{
    if (!KCON_DIRECT) {
        if (redir_on)
            return redir_sink(c);      /* builtin "> file" */
        if (CUR_ROUTE == ROUTE_PIPE)
            return pipe_putc(c);       /* writer 側: カーネルパイプへ */
        if (CUR_ROUTE == ROUTE_DISCARD)
            return c;                  /* per-block /dev/null */
    }
    if ((char)c == '\n')
        PHYS_PUTC('\r');           /* CR 付加 */
    PHYS_PUTC(c);
    return c;
}

static void putstr(const char *s)
{
    while (*s)
        kputchar(*s++);
}

/* コンソール 1 バイト戻しバッファ(#33)。
 *   sh の前景待ちループは Ctrl+C 検出のため con_break() を回し続けるが、
 *   旧実装は **Ctrl+C 以外の 1 バイトを読んで捨てて**いた。前景コマンドが
 *   端末から読む型(対話 cat / vi)だと sh と 1 バイト単位で入力を取り合い、
 *   キーがランダムに消える(user/cat.c の冒頭にも既知の制限として記録あり)。
 *   con_break は Ctrl+C 以外をここへ戻し、kgetchar が物理層より先に返す。
 *   1 バイトで足りるのは、con_ung が埋まっている間 con_break が新たに
 *   読まない(= 2 バイト目を取りに行かない)ため。 */
static int con_ung = -1;

/* 戻しバッファに 1 バイト持っているか。DRIVER の kbhit / getc_timeout 用。 */
int con_pending(void)
{
    return (con_ung >= 0);
}

/* kgetchar: カーネルの 1 バイト入力。物理層を内包。戻り 0..255。 */
int kgetchar(void)
{
    int c;

    if (in_on)
        return in_src();
    if (pipe_is_reader(*(volatile unsigned char *)KW_CURRENT))
        return pipe_getc();       /* reader 側: カーネルパイプから(空なら proc_block) */
    /* 戻しバッファの確認は **待ちループの中**で行う。sh は別ブロックで並行に
     * 走っており、こちらが RXRDY を待っている最中に con_break が横から
     * 1 バイトを con_ung へ移すことがある。ループ外で 1 回見るだけだと
     * その 1 バイトを永久に取り逃してハングする。
     *
     * さらに「con_ung の確認」と「物理層からの取り出し」は **di で束ねる**。
     * 割ってしまうと次の順序が起きて文字が入れ替わる(実測: `vi` と打つと
     * `iv` になる):
     *     kgetchar: con_ung を見る → 空
     *     (プリエンプト) con_break: 'v' を読んで con_ung へ
     *     kgetchar: RXRDY を見る → 次に届いた 'i' を返す   ← 追い越し
     * di 区間は数命令で、FatFs には降りない([[cpmsim-di-fatfs-hang]] に非該当)。 */
    for (;;) {
        IRQ_OFF();
        if (con_ung >= 0) {
            c = con_ung;
            con_ung = -1;
            IRQ_ON();
            return c;
        }
        if (PHYS_RXRDY()) {
            c = PHYS_GETC();       /* 0..255。符号拡張しない */
            IRQ_ON();
            return c;
        }
        IRQ_ON();
        KYIELD();                  /* z80board: 入力待ちの間に他を回す(タイマ無し) */
    }
}

#if defined(KW_CONRAW) && defined(PLAT_FLAT32)
/* con_setraw: 呼んだプロセスの間だけ Ctrl+C を割り込みとして扱わない(on=1)/ 戻す(0)。
 *   tty の raw モード(ISIG 無効)に相当。rx(xmodem)のようにバイナリを端末から
 *   読むコマンドが使う ── シーケンス番号 3 や CRC に 0x03 が現れると、sh の前景待ち
 *   (con_break)がそれを Ctrl+C と取って rx を kill していた。
 *   PLAT_FLAT32: syscall 35。z80 は同じ中身を DRIVER の drv_conraw(drv_tbl[48])に置く
 *   (z80board のカーネル ROM に余地が無いため)。 */
void con_setraw(unsigned char on) __sdcccall(0)
{
    KCONRAW = on ? *(volatile unsigned char *)KW_CURRENT : 0;
}
#endif

/* con_break: コンソールに Ctrl+C(0x03)が来ていれば 1。
 *   前景コマンド実行中の中断検出用(sh の待ちループから非ブロッキングで呼ぶ)。
 *   Ctrl+C 以外の 1 バイトは con_ung へ戻す(捨てない)。
 *   in_on 中(入力リダイレクト)は無効。PHYS_RXRDY() = RX-ready は kgetchar と同義。 */
int con_break(void)
{
    int c;

    /* z80board: sh の前景待ち / krun_wait はここをポーリングする。タイマが
     * 無いので、ここで譲らないと子に CPU が渡らない。 */
    KYIELD();
    if (in_on)         return 0;
#ifdef KW_CONRAW
    /* 生モード中(tty の ISIG を切った状態)は 0x03 をデータとして前景に渡す。
     * 要求したスロットが既に終わっていれば通常どおり(フラグは sh が前景ジョブの後で 0 に)。 */
    if (KCONRAW && ((volatile unsigned char *)KW_PIDTAB)[KCONRAW]) return 0;
#endif
    /* kgetchar 側と同じ理由で di で束ねる(確認 → 取り出し → 退避 が
     * 前景プロセスの kgetchar と交錯すると入力順が入れ替わる)。 */
    IRQ_OFF();
    if (con_ung >= 0)  { IRQ_ON(); return 0; }  /* 保持中。追加で読まない */
    if (!PHYS_RXRDY()) { IRQ_ON(); return 0; }
    c = PHYS_GETC();
    if ((c & 0x7F) == 0x03) { IRQ_ON(); return 1; }
    con_ung = c;
    IRQ_ON();
    return 0;
}

/* 10進固定。putn は依然 iy 破壊の可能性が残る(既知の未解決事項)。 */
static void putn(unsigned int v)
{
    char buf[5];                   /* 65535 = 5 桁 */
    unsigned char i = 0;

    do {
        buf[i++] = (char)('0' + (unsigned char)(v % 10));
        v /= 10;
    } while (v);

    while (i)
        kputchar(buf[--i]);
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    char c;
    int v;

    va_start(ap, fmt);
    while ((c = *fmt++) != 0) {
        if (c != '%') { kputchar(c); continue; }
        switch (c = *fmt++) {
        case 's': putstr(va_arg(ap, char *)); break;
        case 'd':
            v = va_arg(ap, int);
            if (v < 0) { kputchar('-'); v = -v; }
            putn((unsigned int)v);
            break;
        case 'u': putn(va_arg(ap, unsigned int)); break;
        case '%': kputchar('%'); break;
        default:  kputchar('%'); kputchar(c); break;
        }
    }
    va_end(ap);
    return 0;
}

/* readline(行編集)は src/sh.c へ移した(2026-09-30)。呼ぶのはカーネル内蔵の sh だけで、
 * sh を外部コマンドにしたアーキ(z80 / m68k-mega / esp32)では誰も呼ばないのに、
 * ここにあると io.rel と一緒に丸ごとリンクされていた(z80board の ROM で 230 バイト)。 */
