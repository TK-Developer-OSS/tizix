/* user/shvec.h - 外部 sh(user/sh.c → /bin/sh.bin)専用のカーネル入口。
 *
 *   通常コマンドは使わない。sh.c だけが include する。stdio.h と同じく、
 *   カーネルの呼び方が 2 通りあり、コンパイラで分かれる:
 *     SDCC(z80)  … drv_tbl[27..44] の関数ポインタを直接呼ぶ
 *     それ以外(gcc) … syscall5() 経由(番号は src/sysfile.c の 22..34 と同じ)
 *   名前と意味はどちらも同じなので、user/sh.c は 1 本で済む。
 */
#ifndef _SHVEC_H
#define _SHVEC_H

#include "stdio.h"

#if defined(__SDCC)
/* ==================================================================
 * SDCC(z80): drv_tbl[] 直呼び
 *   実体アドレスは DRIVER.BIN リンク時に user/Makefile の FS_SYMS が
 *   kernel.map から -g で drvvec.s の .dw エントリへ束縛する。
 *   全て --sdcccall 0(カーネルと共通 ABI)。
 * ================================================================== */

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

#else
/* ==================================================================
 * gcc(m68k-mega / esp32-wroom-32e): syscall5() 経由
 *
 *   加えて、z80 では数値番地を直に読み書きしている 3 つをここで先に定義する
 *   (user/sh.c 側は #ifndef で z80 の既定を持つ):
 *     SH_STATE          … セッション状態。こちらはコマンドが自分の BSS を
 *                          持てるので static 領域でよい
 *     SH_PID / SH_KILL  … pid 表(カーネルの kwork。番地はリンク時に決まる)
 *     SH_ROUTE          … 出力ルート表
 * ================================================================== */

#define kexec_argv(f, p, c)   ((unsigned char)syscall5(22, (unsigned long)(f), (unsigned long)(p), (unsigned long)(c), 0))
#define builtin_try(c, a)     ((int)syscall5(23, (unsigned long)(c), (unsigned long)(a), 0, 0))
#define builtin_is(c)         ((int)syscall5(24, (unsigned long)(c), 0, 0, 0))
#define redir_begin(f, ap)    ((int)syscall5(25, (unsigned long)(f), (unsigned long)(ap), 0, 0))
#define redir_end()           ((void)syscall5(26, 0, 0, 0, 0))
#define in_begin(f)           ((int)syscall5(27, (unsigned long)(f), 0, 0, 0))
#define in_end()              ((void)syscall5(28, 0, 0, 0, 0))
#define kchdir(p)             ((int)syscall5(30, (unsigned long)(p), 0, 0, 0))
#define kgetcwd(o)            ((void)syscall5(31, (unsigned long)(o), 0, 0, 0))
#define kcon_break()          ((int)syscall5(32, 0, 0, 0, 0))

/* krun_pipe は 6 引数。syscall5 は 4 本しか運べないので配列で渡す。 */
static unsigned char krun_pipe(const char *ln, const char *lp, unsigned char lc,
                               const char *rn, const char *rp, unsigned char rc)
{
        unsigned long a[6];
        a[0] = (unsigned long)ln; a[1] = (unsigned long)lp; a[2] = lc;
        a[3] = (unsigned long)rn; a[4] = (unsigned long)rp; a[5] = rc;
        return (unsigned char)syscall5(29, (unsigned long)a, 0, 0, 0);
}

/* pid 表 / 出力ルート(syscall 20 は /bin/free と共用の「スロット n の pid」) */
#define SH_PID(n)             ((unsigned char)syscall5(20, (unsigned long)(n), 0, 0, 0))
#define SH_KILL(n)            ((void)syscall5(33, (unsigned long)(n), 0, 0, 0))
#define SH_ROUTE(n, r)        ((void)syscall5(34, (unsigned long)(n), (unsigned long)(r), 0, 0))

/* セッション状態(user/sh.c の struct sh_state、約 540B)。 */
#ifndef SHVEC_NO_STATE                    /* sh 以外(tzsh)は持たない(#111) */
#define SH_STATE_SIZE         640
static char sh_state_mem[SH_STATE_SIZE] __attribute__((aligned(4)));
#define SH_STATE              ((unsigned long)sh_state_mem)
#endif

#endif /* __SDCC */

#endif
