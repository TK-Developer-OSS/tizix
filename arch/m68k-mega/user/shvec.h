/* arch/m68k-mega/user/shvec.h - 外部 sh(user/sh.c → /bin/sh.bin)専用のカーネル入口
 *
 *   z80 の user/shvec.h(drv_tbl[27..44] の関数ポインタ)と同じ名前・同じ
 *   意味の入口を、m68k の TRAP #0 syscall(arch/m68k-mega/sysfile.c の 22..34)
 *   で出す。user/sh.c は無改造でこちらを include する(Makefile が tzport へ
 *   stdio.h と一緒に写す)。
 *
 *   加えて、z80 では数値番地を直に読み書きしている 3 つをここで先に定義する
 *   (user/sh.c 側は #ifndef で既定を持つ):
 *     SH_STATE          … セッション状態。m68k はコマンドが自分の BSS を
 *                          持てるので static 領域でよい
 *     SH_PID / SH_KILL  … pid 表(カーネルの kwork。番地はリンク時に決まる)
 *     SH_ROUTE          … 出力ルート表
 */
#ifndef _SHVEC_H
#define _SHVEC_H

#include "stdio.h"

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
#define SH_STATE_SIZE         640
static char sh_state_mem[SH_STATE_SIZE] __attribute__((aligned(4)));
#define SH_STATE              ((unsigned long)sh_state_mem)

#endif
