/* tz/drv16.c : the DRIVER (/bin/driver.bin) -- the stdio every command used to carry, once, in tzcc.
 *   As DRIVER.BIN on the z80 (user/driver.c): commands reach these through the table of tz/drv16e.S
 *   (user/drvtab.h), from user/drvc.S. CS = the DRIVER, DS = SS = the calling process.
 *   No data of its own: FILE objects and the open directory are in the caller's area (user/drvpsp.h), at fixed
 *   offsets, so each process has its own and nothing here needs initialising.
 *   A FILE is 6 words: fd, eof, err, used, pos lo, pos hi (the layout of the FILE typedef in user/tzstdio.h).
 *   tzcc rules (tz/README.md): no initialised globals (there are none), compares and divides are signed (drv_ult /
 *   drv_udiv), a char argument from ia16-gcc is declared char, and a pointer target is always one byte: words go
 *   through drv_peekw / drv_pokew (tz/drv16e.S), and a FILE is handled as its address.
 */
#include "user/drvpsp.h"

unsigned drv_sys(unsigned func, unsigned al, unsigned bx, unsigned cx, unsigned si, unsigned di);
int putchar(int c);
int getchar(void);
void prnum(unsigned v);
unsigned drv_ult(unsigned a, unsigned b);
unsigned drv_udiv(unsigned a, unsigned b);
unsigned drv_peekw(unsigned addr);
void drv_pokew(unsigned addr, unsigned v);

#define F_FD   0                        /* FILE fields, byte offsets */
#define F_EOF  2
#define F_ERR  4
#define F_USED 6
#define F_PLO  8
#define F_PHI  10

/* ---- console ---- */
void prs(char *s)
{
    while (*s) {
        putchar(*s);
        s = s + 1;
    }
}

int puts(char *s)
{
    prs(s);
    putchar(10);
    return 0;
}

void prhex(unsigned v)
{
    unsigned d;
    if (v >> 4)
        prhex(v >> 4);
    d = v & 15;
    if (d < 10)
        putchar(48 + d);
    else
        putchar(87 + d);                /* 'a' - 10 */
}

/* printf's body: %s %d %u %c %x %% (as user/tzstdio.h). ap is the address of the first argument after fmt
 * (printf in tz/drv16e.S works it out); each takes one word. */
int drv_vprintf(char *fmt, unsigned ap)
{
    unsigned v;
    char c;
    while (*fmt) {
        c = *fmt;
        fmt = fmt + 1;
        if (c != '%') {
            putchar(c);
            continue;
        }
        c = *fmt;
        if (c == 0)
            break;
        fmt = fmt + 1;
        if (c == '%') {
            putchar('%');
            continue;
        }
        v = drv_peekw(ap);
        ap = ap + 2;
        if (c == 's') {
            prs(v);
        } else if (c == 'd') {
            if (v & 0x8000) {
                putchar('-');
                v = 0 - v;
            }
            prnum(v);
        } else if (c == 'u') {
            prnum(v);
        } else if (c == 'c') {
            putchar(v);
        } else if (c == 'x') {
            prhex(v);
        } else {
            putchar('%');
            putchar(c);
            ap = ap - 2;                /* an unknown conversion takes no argument */
        }
    }
    return 0;
}

void klog(char *msg)
{
    drv_sys(18, 0, 0, 0, msg, 0);
}

int kbhit(void)
{
    return drv_sys(17, 0, 0, 0, 0, 0);
}

void con_raw(int on)
{
    drv_sys(35, on, 0, 0, 0, 0);
}

int getc_timeout(unsigned wait)
{
    unsigned t0;
    t0 = drv_sys(3, 0, 0, 0, 0, 0);
    for (;;) {
        if (drv_sys(17, 0, 0, 0, 0, 0))
            return getchar();
        if (!drv_ult(drv_sys(3, 0, 0, 0, 0, 0) - t0, wait))
            return -1;
    }
}

/* ---- FILE (f is the FILE's address in the caller's area) ---- */
unsigned f_open_ok(unsigned f)
{
    if (f == 0)
        return 0;
    return drv_peekw(f + F_USED);
}

void fpos_add(unsigned f, unsigned n)
{
    unsigned lo;
    unsigned nl;
    lo = drv_peekw(f + F_PLO);
    nl = lo + n;
    drv_pokew(f + F_PLO, nl);
    if (drv_ult(nl, lo))
        drv_pokew(f + F_PHI, drv_peekw(f + F_PHI) + 1);
}

unsigned fopen(char *path, char *mode)
{
    unsigned flags;
    unsigned r;
    unsigned i;
    unsigned f;
    flags = 0;                          /* syscall 4, AL: 1 = write (create / truncate), 2 = read + write ("r+") */
    if (mode && mode[0] == 'w')
        flags = 1;
    else if (mode && mode[0] == 'r' && mode[1] == '+')
        flags = 2;
    r = drv_sys(4, flags, 0, 0, path, 0);
    if (r == 0xFFFF)
        return 0;
    for (i = 0; i < DRV_NFILE; i++) {
        f = DRV_PSP_FILES + i * DRV_FILE_SIZE;
        if (drv_peekw(f + F_USED) == 0) {
            drv_pokew(f + F_FD, r);
            drv_pokew(f + F_EOF, 0);
            drv_pokew(f + F_ERR, 0);
            drv_pokew(f + F_USED, 1);
            drv_pokew(f + F_PLO, 0);
            drv_pokew(f + F_PHI, 0);
            return f;
        }
    }
    drv_sys(5, r, 0, 0, 0, 0);          /* no free FILE: close the fd again */
    return 0;
}

int fclose(unsigned f)
{
    if (!f_open_ok(f))
        return -1;
    drv_sys(5, drv_peekw(f + F_FD), 0, 0, 0, 0);
    drv_pokew(f + F_USED, 0);
    return 0;
}

/* one byte read into / written from a word on this stack (SS = DS = the caller's segment, where the kernel moves
 * the data) */
int fgetc(unsigned f)
{
    unsigned c;
    if (!f_open_ok(f))
        return -1;
    c = 0;
    if (drv_sys(6, 0, drv_peekw(f + F_FD), 1, 0, &c) != 1) {
        drv_pokew(f + F_EOF, 1);
        return -1;
    }
    fpos_add(f, 1);
    return c & 0xFF;
}

int fputc(int c, unsigned f)
{
    unsigned ch;
    if (!f_open_ok(f))
        return -1;
    ch = c & 0xFF;
    if (drv_sys(7, 0, drv_peekw(f + F_FD), 1, 0, &ch) != 1)
        return -1;
    fpos_add(f, 1);
    return c;
}

unsigned fread(char *ptr, unsigned size, unsigned nmemb, unsigned f)
{
    unsigned want;
    unsigned got;
    if (!f_open_ok(f) || size == 0)
        return 0;
    want = size * nmemb;
    got = drv_sys(6, 0, drv_peekw(f + F_FD), want, 0, ptr);
    if (got == 0xFFFF)
        got = 0;
    fpos_add(f, got);
    if (drv_ult(got, want))
        drv_pokew(f + F_EOF, 1);
    if (size == 1)
        return got;
    return drv_udiv(got, size);
}

unsigned fwrite(char *ptr, unsigned size, unsigned nmemb, unsigned f)
{
    unsigned want;
    unsigned got;
    if (!f_open_ok(f) || size == 0)
        return 0;
    want = size * nmemb;
    got = drv_sys(7, 0, drv_peekw(f + F_FD), want, 0, ptr);
    if (got == 0xFFFF)
        got = 0;
    fpos_add(f, got);
    if (size == 1)
        return got;
    return drv_udiv(got, size);
}

char *fgets(char *buf, int n, unsigned f)
{
    int i;
    int c;
    if (f == 0 || n <= 1)
        return 0;
    i = 0;
    while (i < n - 1) {
        c = fgetc(f);
        if (c == -1) {
            if (i == 0)
                return 0;
            break;
        }
        buf[i] = c;
        i = i + 1;
        if (c == 10)
            break;
    }
    buf[i] = 0;
    return buf;
}

int fputs(char *s, unsigned f)
{
    while (*s) {
        if (fputc(*s, f) == -1)
            return -1;
        s = s + 1;
    }
    return 0;
}

int feof(unsigned f)
{
    if (f == 0)
        return 1;
    return drv_peekw(f + F_EOF);
}

int ferror(unsigned f)
{
    if (f == 0)
        return 1;
    return drv_peekw(f + F_ERR);
}

/* fseek(f, off, whence): off is a long, two words on the stack (low first) */
int fseek(unsigned f, unsigned lo, unsigned hi, int whence)
{
    unsigned r;
    unsigned h;
    if (!f_open_ok(f))
        return -1;
    r = drv_sys(13, whence, drv_peekw(f + F_FD), lo, hi, 0);
    h = drv_sys(13, 3, 0, 0, 0, 0);
    if (r == 0xFFFF && h == 0xFFFF)
        return -1;
    drv_pokew(f + F_PLO, r);
    drv_pokew(f + F_PHI, h);
    drv_pokew(f + F_EOF, 0);
    return 0;
}

/* ---- directory (one open at a time, as the tzcc convention) and the file system ---- */
int opendir(char *path)
{
    unsigned r;
    r = drv_sys(8, 0, 0, 0, path, 0);
    if (r == 0xFFFF)
        return -1;
    drv_pokew(DRV_PSP_DH, r + 1);
    return 0;
}

int readdir(char *name13)
{
    unsigned dh;
    dh = drv_peekw(DRV_PSP_DH);
    if (dh == 0)
        return 0;
    return drv_sys(9, 0, dh - 1, 0, 0, name13);
}

void closedir(void)
{
    unsigned dh;
    dh = drv_peekw(DRV_PSP_DH);
    if (dh)
        drv_sys(10, dh - 1, 0, 0, 0, 0);
    drv_pokew(DRV_PSP_DH, 0);
}

int unlink(char *path)
{
    if (drv_sys(11, 0, 0, 0, path, 0) == 0xFFFF)
        return 1;
    return 0;
}

int mkdir(char *path)
{
    return drv_sys(14, 0, 0, 0, path, 0);
}

int rename(char *a, char *b)
{
    return drv_sys(15, 0, 0, 0, a, b);
}
