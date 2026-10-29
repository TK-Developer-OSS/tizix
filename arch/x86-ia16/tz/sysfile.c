/* tizix x86-ia16 : INT 80h dispatcher -- tzcc version of ../sysfile.c.
 *   crt0.s _isr80 calls sys_call(ax, bx, cx, si, di, cseg) on the slot's syscall stack (SS = DS = kernel, IF = 0).
 *   ax = number << 8 | small argument; pointers (si / di) are offsets in the caller's segment cseg and are moved
 *   with farcpy / farcpy_in. The numbers and their meaning are the ones of ../sysfile.c.
 */
#include "kw.h"
#include "fsx.h"

void farcpy(unsigned dseg, unsigned doff, char *src, unsigned n);
void farcpy_in(char *dst, unsigned sseg, unsigned soff, unsigned n);
char *tz_kwork(void);
unsigned tz_kw_w(unsigned off);
unsigned ult(unsigned a, unsigned b);
unsigned tz_exitcode(unsigned n);
void tz_set_exitcode(unsigned n, unsigned v);
unsigned tz_time_lo(void);
unsigned tz_time_hi(void);
void time_set(unsigned lo, unsigned hi);
unsigned x86_lowword(unsigned off);
unsigned x86_pool_blocks(void);
unsigned x86_blkinfo(unsigned sel);
unsigned x86_scstk_headroom(void);
unsigned x86_kstack_headroom(void);
void tz_kyield(void);
int kputchar(int c);
int kgetchar(void);
int con_rx_ready(void);
int con_pending(void);
int con_break(void);
char kexec_argv(char *fname, char *argpack, char argc);
int builtin_try(char *cmd, char *arg);
int builtin_is(char *cmd);
char krun_pipe(char *ln, char *lp, char lc, char *rn, char *rp, char rc);
int pipe_tail(unsigned want);
int vfs_resolve(char *p);
void dev_open(int kind);
int dev_read(int kind, char *buf, unsigned n);
int dev_write(int kind, char *buf, unsigned n);
int fsb_open(char *abspath, unsigned mode);
int fsb_close(int fh);
int fsb_read(int fh, char *buf, unsigned len);
int fsb_write(int fh, char *buf, unsigned len);
unsigned fsb_lseek(int fh, unsigned whence, unsigned lo, unsigned hi);
unsigned fsb_seek_hi(void);
int fsb_unlink(char *abspath);
int fsb_mkdir(char *abspath);
int fsb_rename(char *src, char *dst);
unsigned kfs_df(unsigned sel);
int kdir_open_abs(char *path);
int kdir_read(char *name);
unsigned kdir_size(void);
void kdir_close(void);
char *kpath(char *in, char slot);
int kchdir(char *path);
void kgetcwd(char *out);
void klog_write(char *msg);
int redir_begin(char *fname, char append);
void redir_end(void);
int in_begin(char *fname);
void in_end(void);

#define NUFD 6
#define PATHMAX 64
#define IOCHUNK 128
#define VFS_FAT (-1)
#define VFS_ENOENT (-2)
#define DEV_NULL 2
#define DEV_FDA 3
#define FSB_OK 0
#define FSB_DENIED (-3)
#define PID_CONT 0xFD

/* user fd table. kind: 0 = free / 1 = FAT (bh = backend handle) / 2 = device (bh = DEV_*) */
char ufd_kind[NUFD];
int ufd_bh[NUFD];
char pathbuf[PATHMAX];
char pathbuf2[PATHMAX];
char iobuf[IOCHUNK];
char namebuf[14];
char pkbuf[160];
char pkbuf2[104];
unsigned sc_a[6];
char cwbuf[56];
unsigned nslots;

/* slots in use: every process takes at least one block, so slot 0 (the kernel) + one per block, at most MAXSLOT.
 * (It was 2 + blocks / 4: 3 slots on 64KB, too few for sh + a & + b &, #216.) */
unsigned x86_nslots(void)
{
    unsigned t;
    if (nslots == 0) {
        t = x86_pool_blocks() + 1;
        if (t > MAXSLOT)
            t = MAXSLOT;
        nslots = t;
    }
    return nslots;
}

void copy_abs(char *buf, char slot)
{
    char *r;
    unsigned i;
    r = kpath(buf, slot);
    if (r != buf) {
        for (i = 0; i < PATHMAX - 1 && r[i]; i++)
            buf[i] = r[i];
        buf[i] = 0;
    }
}

void get_path(unsigned cseg, unsigned off)
{
    farcpy_in(pathbuf, cseg, off, PATHMAX);
    pathbuf[PATHMAX - 1] = 0;
    copy_abs(pathbuf, 0);
}

/* fn 18 / 23 / 24 carry a message, a command name and its argument: copied as they are, not resolved against the cwd */
void get_raw(unsigned cseg, unsigned off)
{
    farcpy_in(pathbuf, cseg, off, PATHMAX);
    pathbuf[PATHMAX - 1] = 0;
}

void get_raw2(unsigned cseg, unsigned off)
{
    farcpy_in(pathbuf2, cseg, off, PATHMAX);
    pathbuf2[PATHMAX - 1] = 0;
}

void get_path2(unsigned cseg, unsigned off)
{
    farcpy_in(pathbuf2, cseg, off, PATHMAX);
    pathbuf2[PATHMAX - 1] = 0;
    copy_abs(pathbuf2, 1);
}

unsigned sc_open(unsigned al)
{
    unsigned u;
    int r;
    int bh;
    r = vfs_resolve(pathbuf);
    if (r == VFS_ENOENT)
        return 0xFFFF;
    for (u = 0; u < NUFD && ufd_kind[u]; u++)
        ;
    if (u == NUFD)
        return 0xFFFF;
    if (r != VFS_FAT) {
        if (r != DEV_NULL && r != DEV_FDA)
            return 0xFFFF;
        dev_open(r);
        ufd_kind[u] = 2;
        ufd_bh[u] = r;
        return u;
    }
    if (al & 1)
        bh = fsb_open(pathbuf, 6);              /* FSB_WRITE | FSB_CREATE */
    else if (al & 2)
        bh = fsb_open(pathbuf, 3);              /* "r+": FSB_READ | FSB_WRITE, the file must exist */
    else
        bh = fsb_open(pathbuf, 1);              /* FSB_READ */
    if (bh < 0)
        return 0xFFFF;
    ufd_kind[u] = 1;
    ufd_bh[u] = bh;
    return u;
}

unsigned sc_read(unsigned u, unsigned n, unsigned di, unsigned cseg)
{
    unsigned done;
    unsigned chunk;
    int got;
    done = 0;
    if (u >= NUFD || ufd_kind[u] == 0)
        return 0;
    while (n) {
        chunk = n;
        if (ult(IOCHUNK, n))
            chunk = IOCHUNK;
        if (ufd_kind[u] == 2)
            got = dev_read(ufd_bh[u], iobuf, chunk);
        else
            got = fsb_read(ufd_bh[u], iobuf, chunk);
        if (got <= 0)
            break;
        farcpy(cseg, di + done, iobuf, got);
        done = done + got;
        n = n - got;
        if (got < chunk)
            break;
    }
    return done;
}

unsigned sc_write(unsigned u, unsigned n, unsigned di, unsigned cseg)
{
    unsigned done;
    unsigned chunk;
    int wr;
    done = 0;
    if (u >= NUFD || ufd_kind[u] == 0)
        return 0;
    while (n) {
        chunk = n;
        if (ult(IOCHUNK, n))
            chunk = IOCHUNK;
        farcpy_in(iobuf, cseg, di + done, chunk);
        if (ufd_kind[u] == 2)
            wr = dev_write(ufd_bh[u], iobuf, chunk);
        else
            wr = fsb_write(ufd_bh[u], iobuf, chunk);
        if (wr <= 0)
            break;
        done = done + wr;
        n = n - wr;
        if (wr < chunk)
            break;
    }
    return done;
}

void proc_release(unsigned n)
{
    char *kw;
    unsigned ns;
    kw = tz_kwork();
    ns = x86_nslots();
    if (n == 0 || n >= ns)
        return;
    kw[n] = 0;
    n = n + 1;
    while (n < ns && kw[n] == PID_CONT) {
        kw[n] = 0;
        n = n + 1;
    }
}

unsigned tz_seg_get(unsigned k);
unsigned sys_call(unsigned ax, unsigned bx, unsigned cx, unsigned si, unsigned di, unsigned cseg)
{
    unsigned fn;
    unsigned al;
    unsigned u;
    unsigned t0;
    int r;
    char *kw;
    char *p;
    fn = ax >> 8;
    al = ax & 0xFF;
    kw = tz_kwork();

    if (fn == 1) {
        kputchar(al);
        return al;
    }
    if (fn == 2) {
        r = kgetchar();
        if (r < 0)
            return 0xFFFF;                      /* EOF of a pipe / "< file" */
        return r & 0xFF;
    }
    if (fn == 3)
        return tz_kw_w(KW_TICKS_O);
    if (fn == 16)
        return tz_time_lo();
    if (fn == 55)
        return tz_time_hi();
    if (fn == 56) {                     /* seg_live(si): 1 if a running slot has this segment (the DRIVER's serial port owner) */
        for (u = 1; u < x86_nslots(); u++)
            if (kw[u] != 0 && tz_seg_get(u) == si)
                return 1;
        return 0;
    }
    if (fn == 54) {
        time_set(bx, cx);
        return 0;
    }
    if (fn == 17) {
        if (con_rx_ready() || con_pending())
            return 1;
        return 0;
    }
    if (fn == 35)
        return 0;                               /* con_raw: not used here */
    if (fn == 4) {
        get_path(cseg, si);
        return sc_open(al);
    }
    if (fn == 5) {
        u = al;
        if (u < NUFD && ufd_kind[u]) {
            if (ufd_kind[u] == 1)
                fsb_close(ufd_bh[u]);
            ufd_kind[u] = 0;
        }
        return 0;
    }
    if (fn == 6)
        return sc_read(bx, cx, di, cseg);
    if (fn == 7)
        return sc_write(bx, cx, di, cseg);
    if (fn == 8) {
        get_path(cseg, si);
        p = pathbuf;
        if (pathbuf[0] == 0 || (pathbuf[0] == '.' && pathbuf[1] == 0))
            p = "/";
        if (kdir_open_abs(p) != 0)
            return 0xFFFF;
        return 0;
    }
    if (fn == 9) {
        r = kdir_read(namebuf);
        if (r <= 0)
            return 0;
        farcpy(cseg, di, namebuf, 13);
        return r;
    }
    if (fn == 10) {
        kdir_close();
        return 0;
    }
    if (fn == 11) {
        get_path(cseg, si);
        if (vfs_resolve(pathbuf) != VFS_FAT)
            return 0xFFFF;
        if (fsb_unlink(pathbuf) == FSB_OK)
            return 0;
        return 0xFFFF;
    }
    if (fn == 12)
        return kdir_size();
    if (fn == 13) {
        if (al == 3)
            return fsb_seek_hi();
        if (bx >= NUFD || ufd_kind[bx] != 1) {
            fsb_lseek(-1, 0, 0, 0);             /* sets the error state */
            return 0xFFFF;
        }
        return fsb_lseek(ufd_bh[bx], al, cx, si);
    }
    if (fn == 14) {
        get_path(cseg, si);
        if (vfs_resolve(pathbuf) != VFS_FAT)
            return 0xFF;
        r = fsb_mkdir(pathbuf);
        if (r == FSB_OK)
            return 0;
        if (r == FSB_DENIED)
            return 0xFF;
        return 1;
    }
    if (fn == 15) {
        get_path(cseg, si);
        get_path2(cseg, di);
        if (vfs_resolve(pathbuf) != VFS_FAT || vfs_resolve(pathbuf2) != VFS_FAT)
            return 0xFF;
        return fsb_rename(pathbuf, pathbuf2);
    }
    if (fn == 18) {
        get_raw(cseg, si);
        klog_write(pathbuf);
        return 0;
    }
    if (fn == 19)
        return kfs_df(al);
    if (fn == 20) {
        if (al >= x86_nslots())
            return 0xFFFF;
        return kw[al];
    }
    if (fn == 21)
        return pipe_tail(al);
    if (fn == 22) {
        get_path(cseg, si);
        farcpy_in(pkbuf, cseg, di, 160);
        pkbuf[159] = 0;
        return kexec_argv(pathbuf, pkbuf, al);
    }
    if (fn == 23) {
        get_raw(cseg, si);
        get_raw2(cseg, di);
        return builtin_try(pathbuf, pathbuf2);
    }
    if (fn == 24) {
        get_raw(cseg, si);
        return builtin_is(pathbuf);
    }
    if (fn == 25) {
        get_path(cseg, si);
        return redir_begin(pathbuf, al);
    }
    if (fn == 26) {
        redir_end();
        return 0;
    }
    if (fn == 27) {
        get_path(cseg, si);
        return in_begin(pathbuf);
    }
    if (fn == 28) {
        in_end();
        return 0;
    }
    if (fn == 29) {
        /* krun_pipe: si = the caller's { ln, lp, lc, rn, rp, rc } */
        farcpy_in(sc_a, cseg, si, 12);
        farcpy_in(pathbuf, cseg, sc_a[0], PATHMAX);
        pathbuf[PATHMAX - 1] = 0;
        farcpy_in(pathbuf2, cseg, sc_a[3], PATHMAX);
        pathbuf2[PATHMAX - 1] = 0;
        farcpy_in(pkbuf2, cseg, sc_a[1], 104);
        pkbuf2[103] = 0;
        farcpy_in(pkbuf, cseg, sc_a[4], 160);
        pkbuf[159] = 0;
        return krun_pipe(pathbuf, pkbuf2, sc_a[2], pathbuf2, pkbuf, sc_a[5]);
    }
    if (fn == 30) {
        get_path(cseg, si);
        return kchdir(pathbuf);
    }
    if (fn == 31) {
        kgetcwd(cwbuf);
        cwbuf[55] = 0;
        farcpy(cseg, di, cwbuf, 49);
        return 0;
    }
    if (fn == 32)
        return con_break();
    if (fn == 33) {
        if (al >= 1 && al < x86_nslots() && kw[al] != PID_CONT) {
            tz_set_exitcode(al, 130);
            proc_release(al);
        }
        return 0;
    }
    if (fn == 34) {
        if (al < x86_nslots())
            kw[KW_OUT_O + al] = bx;
        return 0;
    }
    if (fn == 51) {
        t0 = tz_kw_w(KW_TICKS_O);
        while (ult(tz_kw_w(KW_TICKS_O) - t0, al))
            tz_kyield();
        return 0;
    }
    if (fn == 52) {
        if (al >= x86_nslots())
            return 0xFFFF;
        return tz_exitcode(al);
    }
    if (fn == 53) {
        if (al == 4)
            return x86_nslots();
        if (al >= 6 && al <= 9)
            return x86_blkinfo(al - 6);
        if (al == 11)
            return x86_kstack_headroom();
        if (al == 10)
            return x86_scstk_headroom();
        if (al < 4)
            return x86_lowword(0x512 + 2 * al);
        return 0xFFFF;
    }
    return 0xFFFF;
}
