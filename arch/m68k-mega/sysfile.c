/* arch/m68k-mega/sysfile.c -- TRAP #0 システムコールの C ディスパッチャ
 *   crt0.s の trap0_handler から m68k_sys_call(func, a1, a2, a3, a4) が呼ばれる。
 *   m68k はフラットアドレス空間(セグメントが無い)なので、x86-ia16 の
 *   sysfile.c と違って farcpy 等の移送は不要 ── ポインタ引数はそのまま
 *   カーネル側から読み書きできる。番号は x86-ia16 と同じ約束に揃えてある。
 *
 *   番号:
 *     1 putchar(a1)               -> a1
 *     2 getchar                   -> 文字
 *     3 getticks                  -> tick
 *     4 open(a1=path, a2=flags)   -> fd(0..) / 0xFFFFFFFF
 *     5 close(a1=fd)              -> 0
 *     6 read(a1=fd, a2=buf, a3=n) -> 実読みバイト数
 *     7 write(a1=fd, a2=buf, a3=n)-> 実書きバイト数
 *     8 opendir(a1=path)          -> dh / 0xFFFFFFFF
 *     9 readdir(a1=dh, a2=name13) -> 1(ファイル) / 2(ディレクトリ) / 0(終端)
 *    10 closedir(a1=dh)           -> 0
 *    11 unlink(a1=path)           -> 0 / 0xFFFFFFFF
 *    12 readdir_size()            -> 直前 readdir エントリのサイズ
 *    13 seek(a1=fd,a2=offset,a3=whence) -> 絶対位置 / 0xFFFFFFFF(SEEK_CURは非対応)
 *    14 mkdir(a1=path)            -> 0=ok / 0xFF=denied / 1=その他エラー
 *    15 rename(a1=old,a2=new)     -> 0=ok / 0xFF=denied / 1=その他エラー
 *    16 time_get()                -> Unix 秒(32bit)
 *    17 input_ready()             -> 1=RX に届いているバイトあり / 0=無し
 *       (non-blocking。vi の getc_timeout が ESC シーケンス判定に使う)
 *    18 klog(a1=msg)              -> 0。/var/log/message へ 1 行追記
 *       (z80 の drv_tbl[46] 相当。user/rsyslog.c が使う)
 *    19 df(a1=sel)                -> 0=総容量 KB / 1=空き KB / 0xFFFFFFFF=ボリューム無し
 *       (z80 の drv_tbl[47] 相当。user/df.c が使う)
 *    20 slot(a1=n)                -> pid 表の n 番 / 0xFFFFFFFF=範囲外(/bin/free が使う)
 *    21 pipe_tail(a1=want)        -> 1=パイプ後段として末尾 want 行を出した / 0=後段ではない
 *   22..34: 外部 sh(user/sh.c → /bin/sh.bin)専用。z80 の drv_tbl[27..44](user/shvec.h)と
 *   同じ関数を呼ぶ。ユーザー側の宣言は arch/m68k-mega/user/shvec.h。
 *    22 kexec_argv(fname, argpack, argc) -> スロット / 0=空き無し / 0xFF=ファイル無し
 *    23 builtin_try(cmd, arg)     24 builtin_is(cmd)
 *    25 redir_begin(fname, app)   26 redir_end()   27 in_begin(fname)   28 in_end()
 *    29 krun_pipe(a1=long[6]: ln, lp, lc, rn, rp, rc)
 *    30 kchdir(path) -> 0 / -1    31 kgetcwd(out)  32 con_break() -> 1=Ctrl+C
 *    33 kill(n)                   34 route(n, r): 出力ルート表の n 番を r に
 *    35 con_raw(on): 端末の生モード(Ctrl+C を割り込みにしない。rx が使う)
 *   (0 は exit。trap0_handler が直接処理するのでここには来ない。)
 *
 *   パスを受け取る入口(4 / 8 / 11 / 14 / 15、redir / in)は kpath() で cwd 起点に
 *   解決する(z80 と同じ。cwd はカーネルが持つ、src/fatcmd.c)。
 */
#include "fsbackend.h"
#include "io.h"
#include "kernel.h"
#include "vfs.h"
#include "dev.h"
#include "fatcmd.h"   /* klog_write(case 18) */
#include "kmem.h"     /* KW_PIDTAB / M68K_NSLOT(case 20) */
#include "pipe.h"     /* pipe_tail(case 21) / krun_pipe(case 29) */
#include "kexec.h"    /* kexec_argv(case 22) */
#include "builtin.h"  /* builtin_try / builtin_is(case 23, 24) */

extern int con_rx_ready(void);   /* console.c(同アーキ内)。物理層の RX-ready */

#define NUFD 6
#define IOCHUNK 128

static unsigned char ufd_kind[NUFD];   /* 0=空き 1=FAT 2=DEVFS(dev_ops) 3=生デバイス(kdev、ufd_bh に kind) */
static int           ufd_bh[NUFD];
static const struct dev_ops *ufd_ops[NUFD];
static char iobuf[IOCHUNK];
static char namebuf[13];

unsigned long m68k_sys_call(unsigned long func, unsigned long a1,
                            unsigned long a2, unsigned long a3,
                            unsigned long a4)
{
    switch (func) {
    case 1:
        kputchar((int)(unsigned char)a1);
        return a1;
    case 2: {
        /* #82: パイプの reader は EOF(-1)を受け取る。以前は (unsigned char) で
         * 255 に化けていて、`echo hi | cat` が 0xFF を延々と出し続けた。 */
        int c = kgetchar();
        return (c < 0) ? 0xFFFFFFFFUL : (unsigned long)(unsigned char)c;
    }
    case 3:
        return getticks();

    case 4: {
        const char *path = kpath((const char *)a1, 0);
        unsigned u;
        int r, bh;

        r = vfs_resolve(path);
        if (r == VFS_ENOENT)
            return 0xFFFFFFFFUL;
        for (u = 0; u < NUFD && ufd_kind[u]; u++)
            ;
        if (u == NUFD)
            return 0xFFFFFFFFUL;
        if (r >= 0 && kdev_kind(vfs_node_type(r)) >= 2) {
            /* /dev/fda・/dev/fdb: z80 と同じ kdev_*(src/dev.c)。512B セクタ粒度 */
            ufd_kind[u] = 3;
            ufd_bh[u]   = kdev_open(path, (unsigned char)u);
            return u;
        }
        if (r >= 0) {
            struct vnode *vn = vfs_node(r);
            const struct dev *dv = vn ? (const struct dev *)vn->data : 0;
            if (!dv || !dv->ops)
                return 0xFFFFFFFFUL;
            if (dv->ops->open && dv->ops->open(path, (int)a2) < 0)
                return 0xFFFFFFFFUL;
            ufd_kind[u] = 2;
            ufd_ops[u]  = dv->ops;
            return u;
        }
        /* a2: 1=書き(作成・切り詰め)/ 2=読み書き(既存のみ、fopen "r+")/ 0=読み */
        bh = fsb_open(path, (a2 & 1) ? (FSB_WRITE | FSB_CREATE)
                          : (a2 & 2) ? (FSB_READ | FSB_WRITE) : FSB_READ);
        if (bh < 0)
            return 0xFFFFFFFFUL;
        ufd_kind[u] = 1;
        ufd_bh[u]   = bh;
        return u;
    }
    case 5: {
        unsigned u = (unsigned)a1;
        if (u < NUFD && ufd_kind[u]) {
            if (ufd_kind[u] == 1)
                fsb_close(ufd_bh[u]);
            else if (ufd_kind[u] == 2 && ufd_ops[u] && ufd_ops[u]->close)
                ufd_ops[u]->close(0);
            ufd_kind[u] = 0;
        }
        return 0;
    }
    case 6: {
        unsigned u = (unsigned)a1;
        unsigned n = (unsigned)a3, done = 0;
        unsigned char *dst = (unsigned char *)a2;

        if (u >= NUFD || !ufd_kind[u])
            return 0;
        if (ufd_kind[u] == 3) {
            int got = kdev_stream((unsigned char)ufd_bh[u], 0, dst, n, (unsigned char)u);
            return got < 0 ? 0 : (unsigned long)got;
        }
        while (n) {
            unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
            int got;
            if (ufd_kind[u] == 2)
                got = (ufd_ops[u] && ufd_ops[u]->read) ? ufd_ops[u]->read(0, iobuf, chunk) : -1;
            else
                got = fsb_read(ufd_bh[u], iobuf, chunk);
            if (got <= 0)
                break;
            { unsigned i; for (i = 0; i < (unsigned)got; i++) dst[done + i] = iobuf[i]; }
            done += (unsigned)got;
            n    -= (unsigned)got;
            if ((unsigned)got < chunk)
                break;
        }
        return done;
    }
    case 7: {
        unsigned u = (unsigned)a1;
        unsigned n = (unsigned)a3, done = 0;
        const unsigned char *src = (const unsigned char *)a2;

        if (u >= NUFD || !ufd_kind[u])
            return 0;
        if (ufd_kind[u] == 3) {
            int wr = kdev_stream((unsigned char)ufd_bh[u], 1, (void *)src, n, (unsigned char)u);
            return wr < 0 ? 0 : (unsigned long)wr;
        }
        while (n) {
            unsigned chunk = n > IOCHUNK ? IOCHUNK : n;
            int wr;
            { unsigned i; for (i = 0; i < chunk; i++) iobuf[i] = src[done + i]; }
            if (ufd_kind[u] == 2)
                wr = (ufd_ops[u] && ufd_ops[u]->write) ? ufd_ops[u]->write(0, iobuf, chunk) : -1;
            else
                wr = fsb_write(ufd_bh[u], iobuf, chunk);
            if (wr <= 0)
                break;
            done += (unsigned)wr;
            n    -= (unsigned)wr;
            if ((unsigned)wr < chunk)
                break;
        }
        return done;
    }
    /* 8/9/10/12: ディレクトリ反復は z80 と同じ kdir_*(src/fatcmd.c)を通す(#76)。
     * 以前は fsb_*dir を直に叩いていて /dev が開けず、readdir も種別を捨てて
     * 常に 1 を返していた(ls でディレクトリに / が付かなかった)。
     * kdir は同時 1 個なので dh は常に 0。kdir_open が cwd 起点で解決する。 */
    case 8: {
        const char *p = (const char *)a1;
        if (!p) p = "";
        if (kdir_open(p) != 0)
            return 0xFFFFFFFFUL;
        return 0;
    }
    case 9: {
        char *name13 = (char *)a2;
        int t = kdir_read(namebuf);
        unsigned i;
        (void)a1;
        if (t <= 0)
            return 0;
        for (i = 0; i < 13; i++) name13[i] = namebuf[i];
        return (unsigned long)t;                 /* 1=ファイル / 2=ディレクトリ */
    }
    case 10:
        (void)a1;
        kdir_close();
        return 0;
    case 11: {
        const char *p = kpath((const char *)a1, 0);
        if (vfs_resolve(p) != VFS_FAT)
            return 0xFF;               /* /dev 等: FS_DENIED */
        return (fsb_unlink(p) == FSB_OK) ? 0 : 1;
    }
    case 12:
        return kdir_size();
    case 13: {
        /* seek(a1=fd, a2=offset, a3=whence) -> 移動後の絶対位置 / エラー時 0xFFFFFFFF。
         * SEEK_CUR(1) は現在位置を追跡していないため非対応。du.c の
         * fseek(0,SEEK_END)+ftell によるファイルサイズ取得を通すのが目的。 */
        unsigned u = (unsigned)a1;
        unsigned long newpos;
        if (u < NUFD && ufd_kind[u] == 3) {          /* 生デバイス: SEEK_SET・512 の倍数のみ */
            if (a3 != 0 || kdev_seek((unsigned char)ufd_bh[u], (long)a2, (unsigned char)u) != 0)
                return 0xFFFFFFFFUL;
            return a2;
        }
        if (u >= NUFD || ufd_kind[u] != 1)
            return 0xFFFFFFFFUL;
        if (a3 == 0)       newpos = a2;                             /* SEEK_SET */
        else if (a3 == 2)  newpos = fsb_size(ufd_bh[u]) + a2;        /* SEEK_END */
        else return 0xFFFFFFFFUL;                                   /* SEEK_CUR 等は非対応 */
        if (fsb_seek(ufd_bh[u], newpos) != FSB_OK)
            return 0xFFFFFFFFUL;
        return newpos;
    }
    case 14: {
        /* mkdir(a1=path) -> 0=ok / 0xFF=denied(FS_DENIED) / 1=その他エラー */
        const char *p = kpath((const char *)a1, 0);
        int r;
        if (vfs_resolve(p) != VFS_FAT)
            return 0xFFUL;
        r = fsb_mkdir(p);
        if (r == FSB_OK) return 0;
        return (r == FSB_DENIED) ? 0xFFUL : 1UL;
    }
    case 15: {
        /* rename(a1=oldpath, a2=newpath) -> 0=ok / 0xFF=denied / 1=その他 */
        const char *op = kpath((const char *)a1, 0);   /* 2 パス同時なので枠を分ける */
        const char *np = kpath((const char *)a2, 1);
        int r;
        if (vfs_resolve(op) != VFS_FAT || vfs_resolve(np) != VFS_FAT)
            return 0xFFUL;
        r = fsb_rename(op, np);
        if (r == FSB_OK) return 0;
        return (r == FSB_DENIED) ? 0xFFUL : 1UL;
    }
    case 16:
        /* time_get() -> Unix 秒(32bit)。date.c は z80/x86 の固定番地
         * (KW_EPOCH_SEC = kwork 内の可変オフセット、m68k では絶対番地
         * 0x8522 に一致する保証が無い)を直接 peek していたため、m68k では
         * ここを syscall で仲介する(kernel.c の time_get() を利用)。 */
        return time_get();

    case 17:
        return (unsigned long)(con_rx_ready() ? 1 : 0);

    case 18:
        /* klog(msg): /var/log/message へ "YYYY-MM-DD HH:MM:SS [pid] msg" を
         * 1 行追記する。実体は src/fatcmd.c の klog_write(全 ARCH 共有)。
         * z80 では drv_tbl[46] 経由で同じものを呼んでいる。
         * /etc や /var/log が無い古いディスクでは klog_write が黙って
         * 何もしないので、ここでも成否は返さない。 */
        klog_write((const char *)a1);
        return 0;
    case 19:
        /* df(sel): sel=0 → 総容量 KB / 1 → 空き KB / ボリューム無し 0xFFFFFFFF。
         * 実体は src/fatcmd.c の kfs_df(z80 は drv_tbl[47])。/bin/df が使う。 */
        return kfs_df((unsigned)a1);
    case 20:
        /* slot(n): pid 表の n 番の値(0 = 空き / PID_PIPEBUF = パイプのバッファ枠 /
         * それ以外 = プロセス)。n がスロット数以上なら 0xFFFFFFFF。/bin/free が使う
         * (z80 版の free は pid 表の番地を直接読むが、m68k は kwork の中で番地が固定でない)。 */
        if ((unsigned long)a1 >= M68K_NSLOT)
            return 0xFFFFFFFFUL;
        return ((volatile unsigned char *)KW_PIDTAB)[a1];
    case 21:
        /* pipe_tail(want): パイプ後段の tail。src/pipe.c が writer の完了を待ち、
         * 4KB 窓の末尾 want 行を kputchar で出す(z80 は drv_tbl 経由で同じ関数)。 */
        return pipe_tail((unsigned)a1);

    /* ---- 22..34: 外部 sh 専用(z80 の drv_tbl[27..44] と同じ関数)---- */
    case 22:
        return kexec_argv((const char *)a1, (const char *)a2, (unsigned char)a3);
    case 23:
        return (unsigned long)(long)builtin_try((const char *)a1, (const char *)a2);
    case 24:
        return (unsigned long)(long)builtin_is((const char *)a1);
    case 25:
        return (unsigned long)(long)redir_begin((const char *)a1, (unsigned char)a2);
    case 26:
        redir_end();
        return 0;
    case 27:
        return (unsigned long)(long)in_begin((const char *)a1);
    case 28:
        in_end();
        return 0;
    case 29: {
        /* krun_pipe は writer / reader の両方が終わるまでここで待つ。TRAP は
         * 割込みを止めて入るが、待ちループの con_break が IRQ_ON するので
         * タイマで他スロット(両端のコマンド)へ切り替わる(proc_block と同じ理屈)。 */
        const unsigned long *a = (const unsigned long *)a1;
        return krun_pipe((const char *)a[0], (const char *)a[1], (unsigned char)a[2],
                         (const char *)a[3], (const char *)a[4], (unsigned char)a[5]);
    }
    case 30:
        return (unsigned long)(long)kchdir((const char *)a1);
    case 31:
        kgetcwd((char *)a1);
        return 0;
    case 32:
        return (unsigned long)(con_break() ? 1 : 0);
    case 33:
        /* kill(n): 前景の Ctrl+C。z80 の sh が pid 表へ 0 を直書きするのと同じ。 */
        if ((unsigned long)a1 >= 1 && (unsigned long)a1 < M68K_NSLOT)
            ((volatile unsigned char *)KW_PIDTAB)[a1] = 0;
        return 0;
    case 34:
        if ((unsigned long)a1 < M68K_NSLOT)
            ((volatile unsigned char *)KW_OUTROUTE)[a1] = (unsigned char)a2;
        return 0;
    case 35:
        /* con_raw(on): 端末の生モード(src/io.c con_setraw、z80 は drv_tbl[48])。 */
        con_setraw((unsigned char)(a1 != 0));
        return 0;
    }
    return 0xFFFFFFFFUL;
}
