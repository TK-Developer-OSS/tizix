/* arch/x86-ia16/pipestub.c  --  カーネルパイプ(src/pipe.c)の x86 スタブ
 *
 *   x86-ia16 はまだカーネルパイプ非対応(A | B の本物の結線は将来)。
 *   共有の io.c(kputchar/kgetchar の ROUTE_PIPE 分岐)と sh.c(run_kpipe)が
 *   参照する pipe_* を、無害な既定値で埋めてリンクを通すだけ。
 *
 *   既定値の意味:
 *     pipe_setup()     = 0  → 常に「空きブロック無し」= sh が "out of memory"
 *                              でパイプを中止する(z80 側の失敗時と同じ経路)
 *     pipe_is_reader() = 0  → kgetchar は常に物理コンソールへ(パイプ非経由)
 *     pipe_putc(c)     = c  → 呼ばれても素通し(OUTROUTE は x86 では ROUTE_PIPE に
 *                              ならないので実際には来ない)
 *     pipe_getc()      = -1 → 即 EOF
 *     pipe_ovf()       = 0  → 溢れ扱いにしない
 *     pipe_tail()      = 0  → 「パイプ reader ではない」= 呼び出し元が通常経路へ
 *     krun_pipe()      = 1  → out of memory 扱いで sh に諦めさせる
 *     attach/teardown/note_exit = no-op
 *
 *   ★2026-09-12: pipe.h(#27 カーネルパイプ)の増築で pipe_setup の戻り値
 *   (unsigned char)や pipe_ovf/pipe_tail/krun_pipe が増えていたが、この
 *   スタブは追従しておらず、ビルドキャッシュに古い obj/pipestub.o が
 *   残っていたため気づかれていなかった(クリーンビルドで conflicting
 *   types エラーとして発覚)。pipe.h の現行シグネチャに合わせて更新。
 *
 *   DEVELOP.md「x86 は pipe.c を stub 化して無害にビルド」。
 */
#include "pipe.h"

unsigned char pipe_setup(unsigned char rblk)    { (void)rblk; return 0; }
void pipe_attach_writer(unsigned char wblk)     { (void)wblk; }
void pipe_teardown(void)                        { }
int  pipe_putc(int c)                           { return c; }
int  pipe_getc(void)                            { return -1; }
unsigned char pipe_is_reader(unsigned char blk) { (void)blk; return 0; }
unsigned char pipe_ovf(void)                    { return 0; }
unsigned char pipe_tail(unsigned want)          { (void)want; return 0; }
void pipe_note_exit(unsigned char blk)          { (void)blk; }

unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                        const char *rn, const char *rp, unsigned char rc)
{
	(void)ln; (void)lp; (void)lc; (void)rn; (void)rp; (void)rc;
	return 1;       /* out of memory 扱い */
}
