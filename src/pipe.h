#ifndef _PIPE_H
#define _PIPE_H

/* ==================================================================
 * pipe.h - カーネルパイプ(#27: 4KB ブロックバッファ版)
 *
 *   同時 1 本。pipe_setup がプロセス枠の空きブロックを 1 個確保して
 *   線形バッファ(4096B、wrap 無し)にする。teardown で解放。
 *   総量が 4096B を超えたら ovf(sh が "out of memory" で中断)。
 *   4096B 超の streaming は malloc 実装後(task #27 項目 a)。
 *
 *   両側が外部コマンドのパイプ(sh の run_kpipe)専用。片側 builtin の
 *   パイプは従来の一時ファイル方式(sh 側)。
 *
 *   routing: writer の putchar は kputchar が OUTROUTE[block]==ROUTE_PIPE で
 *            pipe_putc へ。reader の getchar は kgetchar が pipe_is_reader() で
 *            pipe_getc へ。コマンドはパイプを意識しない(透過)。
 * ================================================================== */

/* reader を launch した直後に pipe_setup(rblk)。
 *   戻り 1=OK / 0=空きブロック無し(sh は "out of memory" でパイプ中止)。
 *   その後 writer を launch → pipe_attach_writer(wblk) → OUTROUTE[wblk]=ROUTE_PIPE。 */
unsigned char pipe_setup(unsigned char rblk) __sdcccall(0);
void pipe_attach_writer(unsigned char wblk) __sdcccall(0);
void pipe_teardown(void) __sdcccall(0);          /* バッファブロックも解放 */

/* io.c の kputchar / kgetchar から呼ぶ。 */
int  pipe_putc(int c) __sdcccall(0);          /* 追記。戻り c / -1(満杯 or reader 消滅) */
int  pipe_getc(void) __sdcccall(0);           /* 取り出し。空なら proc_block。戻り 0..255 / -1(EOF) */
unsigned char pipe_is_reader(unsigned char blk) __sdcccall(0);

/* 総量が 4096B を超えたか(sh の run_kpipe 待ちループが検出して中断)。 */
unsigned char pipe_ovf(void) __sdcccall(0);

/* パイプ後段 tail 用: writer 完了まで待って 4KB 窓を後方スキャンし、末尾
 *   want 行を kputchar 出力する。戻り 1=処理した / 0=パイプ reader ではない。 */
unsigned char pipe_tail(unsigned want) __sdcccall(0);

/* sh の run_kpipe 待ちループが、パイプ相手の終了(PIDTBL==0)を見て呼ぶ。
 * writer 終了 → weof、reader 終了 → rgone(相手を起こす)。 */
void pipe_note_exit(unsigned char blk) __sdcccall(0);

/* krun_pipe: A | B(両側外部)の起動・結線・監視をカーネル側で行う(#27)。
 *   sh の run_kpipe が drv_tbl 経由で 1 回呼ぶ。ln/rn は bare コマンド名。
 *   戻り 0=正常 / 1=out of memory / 2=ovf(4KB 超で打ち切り) / 3=Ctrl+C。 */
unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                        const char *rn, const char *rp, unsigned char rc) __sdcccall(0);

#endif
