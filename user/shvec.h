/* user/shvec.h - 外部 sh(user/sh.c → /bin/sh.bin)専用のカーネル入口。
 *
 *   drv_tbl[27..37]。通常コマンドは使わない。sh.c だけが include する。
 *   実体アドレスは DRIVER.BIN リンク時に user/Makefile の FS_SYMS が
 *   kernel.map から -g で drvvec.s の .dw エントリへ束縛する。
 *   全て --sdcccall 0(カーネルと共通 ABI)。
 */
#ifndef _SHVEC_H
#define _SHVEC_H

#include "stdio.h"      /* drv_tbl[] */

typedef unsigned char (*fn_kexec_argv_t)(const char *, const char *, unsigned char) __sdcccall(0);
typedef int  (*fn_btry_t)(const char *, const char *) __sdcccall(0);
typedef int  (*fn_bis_t)(const char *) __sdcccall(0);
typedef int  (*fn_rbegin_t)(const char *) __sdcccall(0);              /* in_begin */
typedef int  (*fn_redir_t)(const char *, unsigned char) __sdcccall(0); /* redir_begin(name, append) */
typedef void (*fn_rvoid_t)(void) __sdcccall(0);
typedef void (*fn_pblk_t)(unsigned char) __sdcccall(0);
typedef unsigned char (*fn_psetup_t)(unsigned char) __sdcccall(0);  /* pipe_setup: 0=空き無し */
typedef unsigned char (*fn_povf_t)(void) __sdcccall(0);             /* pipe_ovf */
typedef unsigned char (*fn_krunp_t)(const char *, const char *, unsigned char,
                                    const char *, const char *, unsigned char) __sdcccall(0);

#define kexec_argv(f, p, c)   (((fn_kexec_argv_t)drv_tbl[27])((f), (p), (c)))
#define builtin_try(c, a)     (((fn_btry_t)drv_tbl[28])((c), (a)))
#define builtin_is(c)         (((fn_bis_t)drv_tbl[29])((c)))
#define redir_begin(f, ap)    (((fn_redir_t)drv_tbl[30])((f), (unsigned char)(ap)))
#define redir_end()           (((fn_rvoid_t)drv_tbl[31])())
#define in_begin(f)           (((fn_rbegin_t)drv_tbl[32])((f)))
#define in_end()              (((fn_rvoid_t)drv_tbl[33])())
#define pipe_setup(r)         (((fn_psetup_t)drv_tbl[34])((unsigned char)(r)))
#define pipe_attach_writer(w) (((fn_pblk_t)drv_tbl[35])((unsigned char)(w)))
#define pipe_teardown()       (((fn_rvoid_t)drv_tbl[36])())
#define pipe_note_exit(b)     (((fn_pblk_t)drv_tbl[37])((unsigned char)(b)))
#define pipe_ovf()            (((fn_povf_t)drv_tbl[39])())
#define krun_pipe(ln,lp,lc,rn,rp,rc) \
        (((fn_krunp_t)drv_tbl[41])((ln),(lp),(unsigned char)(lc),(rn),(rp),(unsigned char)(rc)))

/* #28: カレントディレクトリはカーネルが持つ(src/fatcmd.c)。sh は cd/pwd を
 * これで叩くだけで、引数の絶対化には一切関与しない。 */
typedef int  (*fn_kchdir_t)(const char *) __sdcccall(0);
typedef void (*fn_kgetcwd_t)(char *) __sdcccall(0);
#define kchdir(p)             (((fn_kchdir_t)drv_tbl[42])((p)))
#define kgetcwd(o)            (((fn_kgetcwd_t)drv_tbl[43])((o)))

/* #33: 前景待ちの Ctrl+C 検出はカーネルの con_break を使う。
 * sh が自前で kbhit()+getchar() していた旧 sh_break は Ctrl+C 以外の 1 バイトを
 * **読んで捨てて**いたため、端末から読む前景コマンド(対話 cat / vi)と入力を
 * 取り合ってキーを落としていた。カーネル版は Ctrl+C 以外を戻しバッファへ
 * 退避し、次の kgetchar が拾う。 */
typedef int (*fn_conbrk_t)(void) __sdcccall(0);
#define kcon_break()          (((fn_conbrk_t)drv_tbl[44])())

#endif
