/* tizix x86-ia16 : file system -- tzcc version of ../fat12min.c + ../../src/fsbackend_fat.c + ../../src/fatcmd.c +
 * ../../src/vfs.c + ../../src/dev.c + ../dev_fda.c, in one module (tzcc cannot share data between objects).
 *
 * FAT12 only, 8.3 names, one volume, no partition (superfloppy). One 512B window (win).
 * tzcc has no 32-bit type: every sector / cluster number fits 16 bits on a floppy (< 2880), and the 32-bit file
 * position and size are kept as two words (lo, hi). 16-bit values are compared as signed by tzcc, so a value that can
 * reach 0x8000 is compared with ult() (glue.S). Error sentinel 0xFFFF is always tested with == first.
 * Pointers to int: tzcc reads `*p` as a byte; int arrays are reached only by index through a global pointer or array.
 *
 * /dev is not a tree any more: "/dev" (dir), "/dev/null", "/dev/fda" (raw floppy, read only), "/dev/fdb" (listed only).
 */
#include "kw.h"

unsigned ult(unsigned a, unsigned b);
unsigned tz_udiv(unsigned a, unsigned b);
char *tz_kwork(void);
unsigned tz_kw_w(unsigned off);
int disk_initialize(char pdrv);
int disk_read(char pdrv, char *buff, unsigned sec_lo, unsigned sec_hi, unsigned count);
int disk_write(char pdrv, char *buff, unsigned sec_lo, unsigned sec_hi, unsigned count);
void kprintf(char *fmt, ...);
void redir_enable(char on);
void in_enable(char on);

#define SS 512
#define EOC 0xFF8
#define FIXED_DATE 0x5A77

/* FRESULT values (FatFs numbering; rm / mkdir / mv print them) */
#define FR_OK 0
#define FR_DISK_ERR 1
#define FR_INT_ERR 2
#define FR_NOT_READY 3
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_INVALID_NAME 6
#define FR_DENIED 7
#define FR_EXIST 8
#define FR_NO_FILESYSTEM 13

#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_NEW 4
#define FA_CREATE_ALWAYS 8
#define FA_OPEN_ALWAYS 0x10
#define FA_OPEN_APPEND 0x30
#define AM_RDO 1
#define AM_DIR 0x10
#define AM_ARC 0x20

#define FSB_OK 0
#define FSB_ERR (-1)
#define FSB_NOENT (-2)
#define FSB_DENIED (-3)
#define FSB_NOSPC (-4)

#define VFS_FAT (-1)
#define VFS_ENOENT (-2)
#define DEV_DIR 1
#define DEV_NULL 2
#define DEV_FDA 3
#define DEV_FDB 4

/* an open file (also the opaque 32-byte FIL of tz/kexec.c) */
struct fil {
    unsigned plo;           /* position */
    unsigned phi;
    unsigned slo;           /* size */
    unsigned shi;
    unsigned sclust;        /* first cluster, 0 = empty */
    unsigned clust;         /* cluster holding the position (or the one before) */
    unsigned cidx;          /* index of clust in the chain */
    unsigned dcl;           /* directory of the entry: first cluster (0 = root) */
    unsigned didx;          /*   and entry number */
    char flag;              /* FA_READ / FA_WRITE, 0x40 = entry needs an update */
    char err;
};

/* volume */
unsigned v_csize;
unsigned v_cshift;
unsigned v_nfats;
unsigned v_nroot;
unsigned v_nfatent;
unsigned v_fatsz;
unsigned v_fatbase;
unsigned v_dirbase;
unsigned v_database;
unsigned v_hint;
char win[512];
unsigned winsect;
char winvalid;
char windirty;
char tmpent[32];

/* ---------------- window ---------------- */
int flush(void)
{
    unsigned k;
    if (windirty) {
        if (disk_write(0, win, winsect, 0, 1) != 0)
            return FR_DISK_ERR;
        if (!ult(winsect, v_fatbase) && ult(winsect, v_fatbase + v_fatsz)) {    /* FAT1 -> the other copies */
            for (k = 1; k < v_nfats; k++)
                if (disk_write(0, win, winsect + k * v_fatsz, 0, 1) != 0)
                    return FR_DISK_ERR;
        }
        windirty = 0;
    }
    return FR_OK;
}

int move_window(unsigned sect)
{
    if (winvalid && sect == winsect)
        return FR_OK;
    if (flush() != FR_OK)
        return FR_DISK_ERR;
    if (disk_read(0, win, sect, 0, 1) != 0) {
        winvalid = 0;
        return FR_DISK_ERR;
    }
    winsect = sect;
    winvalid = 1;
    return FR_OK;
}

int window_noread(unsigned sect)        /* for a sector that is overwritten as a whole */
{
    if (winvalid && sect == winsect)
        return FR_OK;
    if (flush() != FR_OK)
        return FR_DISK_ERR;
    winsect = sect;
    winvalid = 1;
    return FR_OK;
}

/* ---------------- FAT12 ---------------- */
int fat_byte(unsigned off)              /* -1 = error */
{
    if (move_window(v_fatbase + (off >> 9)) != FR_OK)
        return -1;
    return win[off & 511];
}

int fat_set(unsigned off, unsigned mask, unsigned val)
{
    unsigned i;
    if (move_window(v_fatbase + (off >> 9)) != FR_OK)
        return FR_DISK_ERR;
    i = off & 511;
    win[i] = (win[i] & (mask ^ 0xFF)) | (val & mask);
    windirty = 1;
    return FR_OK;
}

unsigned get_fat(unsigned c)            /* 0xFFFF = cannot read */
{
    unsigned off;
    int b0;
    int b1;
    off = c + (c >> 1);
    b0 = fat_byte(off);
    if (b0 < 0)
        return 0xFFFF;
    b1 = fat_byte(off + 1);
    if (b1 < 0)
        return 0xFFFF;
    if (c & 1)
        return (b0 >> 4) | (b1 << 4);
    return b0 | ((b1 & 15) << 8);
}

int put_fat(unsigned c, unsigned v)
{
    unsigned off;
    off = c + (c >> 1);
    if (c & 1) {
        if (fat_set(off, 0xF0, v << 4) != FR_OK)
            return FR_DISK_ERR;
        return fat_set(off + 1, 0xFF, v >> 4);
    }
    if (fat_set(off, 0xFF, v) != FR_OK)
        return FR_DISK_ERR;
    return fat_set(off + 1, 0x0F, v >> 8);
}

unsigned clust2sect(unsigned c)
{
    return v_database + ((c - 2) << v_cshift);
}

unsigned alloc_cluster(unsigned prev)   /* 0 = none free / error */
{
    unsigned c;
    unsigned n;
    unsigned v;
    n = v_nfatent - 2;
    c = v_hint;
    if (c < 2 || c >= v_nfatent)
        c = 2;
    while (n) {
        n = n - 1;
        v = get_fat(c);
        if (v == 0xFFFF)
            return 0;
        if (v == 0) {
            if (put_fat(c, 0xFFF) != FR_OK)
                return 0;
            if (prev && put_fat(prev, c) != FR_OK)
                return 0;
            v_hint = c + 1;
            return c;
        }
        c = c + 1;
        if (c >= v_nfatent)
            c = 2;
    }
    return 0;
}

void remove_chain(unsigned c)
{
    unsigned nx;
    while (c >= 2 && c < EOC) {
        nx = get_fat(c);
        if (nx == 0xFFFF)
            return;
        put_fat(c, 0);
        if (c < v_hint)
            v_hint = c;
        c = nx;
    }
}

int zero_cluster(unsigned c)
{
    unsigned s;
    unsigned k;
    unsigned i;
    s = clust2sect(c);
    for (k = 0; k < v_csize; k++) {
        if (window_noread(s + k) != FR_OK)
            return FR_DISK_ERR;
        for (i = 0; i < SS; i++)
            win[i] = 0;
        windirty = 1;
    }
    return FR_OK;
}

/* ---------------- directories ---------------- */
/* the 32-byte entry idx of directory dcl (0 = root), in the window; 0 = past the end / error */
char *dir_ptr(unsigned dcl, unsigned idx)
{
    unsigned sect;
    unsigned epc;
    unsigned ci;
    unsigned c;
    if (dcl == 0) {
        if (idx >= v_nroot)
            return 0;
        sect = v_dirbase + (idx >> 4);
    } else {
        epc = v_csize << 4;                     /* entries per cluster (a power of 2) */
        ci = (idx >> 4) >> v_cshift;
        c = dcl;
        while (ci) {
            ci = ci - 1;
            c = get_fat(c);
            if (c == 0xFFFF || c < 2 || c >= EOC)
                return 0;
        }
        sect = clust2sect(c) + ((idx & (epc - 1)) >> 4);
    }
    if (move_window(sect) != FR_OK)
        return 0;
    return win + ((idx & 15) << 5);
}

unsigned ld16(char *p)
{
    return p[0] | (p[1] << 8);
}

void st16(char *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}

/* next path element as an 8.3 name (11 bytes, upper case, blank padded) into nm.
 * nn_p is the cursor; 1 = got one, 0 = end, -1 = invalid.
 * nn_case: the NT case flags of byte 12 of an entry (0x08 = the name part is lower case, 0x10 = the extension is), the way
 * mtools / Windows / FatFs keep an all-lower-case 8.3 name ("bin" is stored "BIN" + 0x08). A mixed-case part gets none. */
char *nn_p;
unsigned nn_case;
int next_name(char *nm)
{
    char *p;
    unsigned i;
    unsigned ext;
    unsigned lim;
    unsigned c;
    unsigned lo;
    unsigned up;
    unsigned xlo;
    unsigned xup;
    lo = 0;
    up = 0;
    xlo = 0;
    xup = 0;
    p = nn_p;
    while (*p == '/')
        p = p + 1;
    if (*p == 0) {
        nn_p = p;
        return 0;
    }
    for (i = 0; i < 11; i++)
        nm[i] = ' ';
    i = 0;
    ext = 0;
    while (*p && *p != '/') {
        c = *p;
        p = p + 1;
        if (c == '.') {
            if (ext || i == 0)
                return -1;
            ext = 1;
            i = 8;
            continue;
        }
        if (c < 0x21 || c == '"' || c == '*' || c == '+' || c == ',' || c == ':' || c == ';' || c == '<' || c == '='
            || c == '>' || c == '?' || c == '[' || c == 92 || c == ']' || c == '|' || c == 0x7F)
            return -1;
        if (c >= 'a' && c <= 'z') {
            c = c - 32;
            if (ext)
                xlo = 1;
            else
                lo = 1;
        } else if (c >= 'A' && c <= 'Z') {
            if (ext)
                xup = 1;
            else
                up = 1;
        }
        lim = 8;
        if (ext)
            lim = 11;
        if (i >= lim)
            return -1;
        nm[i] = c;
        i = i + 1;
    }
    nn_p = p;
    nn_case = 0;
    if (lo && up == 0)
        nn_case = 8;
    if (xlo && xup == 0)
        nn_case = nn_case | 0x10;
    return 1;
}

int name_eq(char *e, char *nm)
{
    unsigned i;
    for (i = 0; i < 11; i++)
        if (e[i] != nm[i])
            return 0;
    return 1;
}

/* look for nm in directory dcl: 1 = found (df_idx), 0 = not there */
unsigned df_idx;
int dir_find(unsigned dcl, char *nm)
{
    unsigned idx;
    char *e;
    idx = 0;
    for (;;) {
        e = dir_ptr(dcl, idx);
        if (e == 0)
            return 0;
        if (e[0] == 0)
            return 0;
        if (e[0] != 0xE5 && (e[11] & 8) == 0 && e[11] != 0x0F && name_eq(e, nm)) {
            df_idx = idx;
            return 1;
        }
        idx = idx + 1;
    }
}

/* split a path into the parent directory (w_dcl) and the last name (w_nm); w_isroot = the path is "/" */
unsigned w_dcl;
char w_nm[11];
char w_nxt[11];
char w_isroot;
unsigned w_case;                        /* nn_case of w_nm */
int walk(char *path)
{
    char *q;
    char *e;
    unsigned dcl;
    int r;
    unsigned k;
    w_isroot = 0;
    dcl = 0;
    nn_p = path;
    r = next_name(w_nm);
    w_case = nn_case;
    if (r < 0)
        return FR_INVALID_NAME;
    if (r == 0) {
        w_isroot = 1;
        w_dcl = 0;
        return FR_OK;
    }
    for (;;) {
        q = nn_p;
        r = next_name(w_nxt);
        if (r < 0)
            return FR_INVALID_NAME;
        if (r == 0)
            break;                              /* w_nm is the last element */
        if (dir_find(dcl, w_nm) == 0)           /* w_nm is a directory on the way */
            return FR_NO_PATH;
        e = dir_ptr(dcl, df_idx);
        if (e == 0 || (e[11] & AM_DIR) == 0)
            return FR_NO_PATH;
        dcl = ld16(e + 26);
        for (k = 0; k < 11; k++)
            w_nm[k] = w_nxt[k];
        w_case = nn_case;
    }
    w_dcl = dcl;
    return FR_OK;
}

/* a free entry (0x00 or 0xE5) in dcl -> da_idx; a full subdirectory grows by one cluster */
unsigned da_idx;
int dir_alloc(unsigned dcl)
{
    unsigned idx;
    unsigned c;
    unsigned last;
    char *e;
    idx = 0;
    for (;;) {
        e = dir_ptr(dcl, idx);
        if (e == 0) {
            if (dcl == 0)
                return FR_DENIED;               /* the root has a fixed size */
            last = dcl;
            for (;;) {
                c = get_fat(last);
                if (c == 0xFFFF || c < 2 || c >= EOC)
                    break;
                last = c;
            }
            c = alloc_cluster(last);
            if (c == 0)
                return FR_DENIED;
            if (zero_cluster(c) != FR_OK)
                return FR_DISK_ERR;
            e = dir_ptr(dcl, idx);
            if (e == 0)
                return FR_INT_ERR;
        }
        if (e[0] == 0 || e[0] == 0xE5) {
            da_idx = idx;
            return FR_OK;
        }
        idx = idx + 1;
    }
}

unsigned fe_case;                       /* NT case flags for the next fill_entry (then back to 0) */
void fill_entry(char *e, char *nm, unsigned attr, unsigned clust)
{
    unsigned i;
    for (i = 0; i < 32; i++)
        e[i] = 0;
    for (i = 0; i < 11; i++)
        e[i] = nm[i];
    e[11] = attr;
    e[12] = fe_case;
    fe_case = 0;
    st16(e + 24, FIXED_DATE);
    st16(e + 26, clust);
    windirty = 1;
}

/* ---------------- mount ---------------- */
int f_mount(void)
{
    unsigned tot;
    unsigned rsv;
    unsigned spc;
    unsigned nf;
    unsigned fz;
    winvalid = 0;
    windirty = 0;
    if (disk_initialize(0) & 1)
        return FR_NOT_READY;
    if (move_window(0) != FR_OK)
        return FR_DISK_ERR;
    if (win[510] != 0x55 || win[511] != 0xAA || ld16(win + 11) != SS)
        return FR_NO_FILESYSTEM;
    spc = win[13];
    rsv = ld16(win + 14);
    nf = win[16];
    tot = ld16(win + 19);                       /* a floppy always has the 16-bit count */
    fz = ld16(win + 22);
    if (spc == 0 || (spc & (spc - 1)) || nf == 0 || fz == 0 || tot == 0)
        return FR_NO_FILESYSTEM;
    v_csize = spc;
    v_cshift = 0;
    while ((1 << v_cshift) < spc)
        v_cshift = v_cshift + 1;
    v_nfats = nf;
    v_nroot = ld16(win + 17);
    v_fatsz = fz;
    v_fatbase = rsv;
    v_dirbase = rsv + nf * fz;
    v_database = v_dirbase + ((v_nroot + 15) >> 4);
    if (ult(tot, v_database))
        return FR_NO_FILESYSTEM;
    v_nfatent = ((tot - v_database) >> v_cshift) + 2;
    if (v_nfatent > 4085)
        return FR_NO_FILESYSTEM;                /* FAT12 only */
    v_hint = 2;
    return FR_OK;
}

/* ---------------- files ---------------- */
int sync_entry(struct fil *fp)
{
    char *e;
    if ((fp->flag & 0x40) == 0)
        return FR_OK;
    e = dir_ptr(fp->dcl, fp->didx);
    if (e == 0)
        return FR_INT_ERR;
    e[11] = e[11] | AM_ARC;
    st16(e + 26, fp->sclust);
    st16(e + 28, fp->slo);
    st16(e + 30, fp->shi);
    st16(e + 24, FIXED_DATE);
    st16(e + 22, 0);
    windirty = 1;
    fp->flag = fp->flag & 0xBF;
    return FR_OK;
}

int f_lseek2(struct fil *fp, unsigned lo, unsigned hi);

char f_open(struct fil *fp, char *path, char mode)
{
    unsigned pdcl;
    unsigned idx;
    unsigned c;
    char *e;
    int fr;
    fr = walk(path);
    if (fr != FR_OK)
        return fr;
    if (w_isroot)
        return FR_INVALID_NAME;
    pdcl = w_dcl;
    if (dir_find(pdcl, w_nm)) {
        idx = df_idx;
        e = dir_ptr(pdcl, idx);
        if (e == 0)
            return FR_DISK_ERR;
        if (e[11] & AM_DIR)
            return FR_NO_FILE;
        if (mode & FA_CREATE_NEW)
            return FR_EXIST;
        if ((mode & FA_WRITE) && (e[11] & AM_RDO))
            return FR_DENIED;
        if (mode & FA_CREATE_ALWAYS) {          /* drop the contents */
            c = ld16(e + 26);
            st16(e + 26, 0);
            st16(e + 28, 0);
            st16(e + 30, 0);
            e[11] = e[11] | AM_ARC;
            windirty = 1;
            remove_chain(c);
            e = dir_ptr(pdcl, idx);
            if (e == 0)
                return FR_DISK_ERR;
        }
    } else {
        if ((mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS)) == 0)
            return FR_NO_FILE;
        fr = dir_alloc(pdcl);
        if (fr != FR_OK)
            return fr;
        idx = da_idx;
        e = dir_ptr(pdcl, idx);
        if (e == 0)
            return FR_DISK_ERR;
        fe_case = w_case;
        fill_entry(e, w_nm, AM_ARC, 0);
    }
    fp->sclust = ld16(e + 26);
    fp->slo = ld16(e + 28);
    fp->shi = ld16(e + 30);
    fp->plo = 0;
    fp->phi = 0;
    fp->clust = fp->sclust;
    fp->cidx = 0;
    fp->dcl = pdcl;
    fp->didx = idx;
    fp->flag = mode & (FA_READ | FA_WRITE);
    fp->err = 0;
    if ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND)
        return f_lseek2(fp, fp->slo, fp->shi);
    return FR_OK;
}

/* sector index of the position inside the file (fits 16 bits on a floppy) */
unsigned pos_sect(struct fil *fp)
{
    return (fp->phi << 7) | (fp->plo >> 9);
}

/* make fp->clust the cluster that holds the position; make != 0 appends clusters as needed */
int seek_clust(struct fil *fp, unsigned make)
{
    unsigned ci;
    unsigned c;
    unsigned nx;
    ci = pos_sect(fp) >> v_cshift;
    if (fp->sclust == 0) {
        if (make == 0)
            return FR_INT_ERR;
        c = alloc_cluster(0);
        if (c == 0)
            return FR_DENIED;
        fp->sclust = c;
        fp->clust = c;
        fp->cidx = 0;
        fp->flag = fp->flag | 0x40;
    }
    if (ci < fp->cidx) {
        fp->clust = fp->sclust;
        fp->cidx = 0;
    }
    while (fp->cidx < ci) {
        nx = get_fat(fp->clust);
        if (nx == 0xFFFF)
            return FR_DISK_ERR;
        if (nx < 2 || nx >= EOC) {
            if (make == 0)
                return FR_INT_ERR;
            nx = alloc_cluster(fp->clust);
            if (nx == 0)
                return FR_DENIED;
        }
        fp->clust = nx;
        fp->cidx = fp->cidx + 1;
    }
    return FR_OK;
}

/* position += n */
void pos_add(struct fil *fp, unsigned n)
{
    unsigned o;
    o = fp->plo;
    fp->plo = o + n;
    if (ult(fp->plo, o))
        fp->phi = fp->phi + 1;
}

/* 1 if position < size */
unsigned pos_lt_size(struct fil *fp)
{
    if (fp->phi != fp->shi)
        return ult(fp->phi, fp->shi);
    return ult(fp->plo, fp->slo);
}

/* f_read: the byte count goes to rw_n (tz/kexec.c passes a pointer to its own word: rw_out) */
unsigned rw_n;
char f_read(struct fil *fp, char *buf, unsigned btr, char *brp)
{
    unsigned off;
    unsigned n;
    unsigned i;
    unsigned sect;
    unsigned dlo;
    int fr;
    rw_n = 0;
    brp[0] = 0;
    brp[1] = 0;
    if ((fp->flag & FA_READ) == 0)
        return FR_DENIED;
    if (pos_lt_size(fp) == 0)
        return FR_OK;
    dlo = fp->slo - fp->plo;                    /* bytes left; only matters when it is below 64KB */
    if (fp->shi - fp->phi - ult(fp->slo, fp->plo) == 0 && ult(dlo, btr))
        btr = dlo;
    while (btr) {
        off = fp->plo & 511;
        n = SS - off;
        fr = seek_clust(fp, 0);
        if (fr != FR_OK) {
            fp->err = fr;
            return fr;
        }
        sect = clust2sect(fp->clust) + (pos_sect(fp) & (v_csize - 1));
        if (move_window(sect) != FR_OK)
            return FR_DISK_ERR;
        if (ult(btr, n))
            n = btr;
        for (i = 0; i < n; i++)
            buf[i] = win[off + i];
        buf = buf + n;
        btr = btr - n;
        rw_n = rw_n + n;
        pos_add(fp, n);
    }
    brp[0] = rw_n;
    brp[1] = rw_n >> 8;
    return FR_OK;
}

char f_write(struct fil *fp, char *buf, unsigned btw)
{
    unsigned off;
    unsigned n;
    unsigned i;
    unsigned sect;
    int fr;
    rw_n = 0;
    if ((fp->flag & FA_WRITE) == 0)
        return FR_DENIED;
    while (btw) {
        off = fp->plo & 511;
        n = SS - off;
        fr = seek_clust(fp, 1);
        if (fr != FR_OK) {
            fp->err = fr;
            return fr;
        }
        sect = clust2sect(fp->clust) + (pos_sect(fp) & (v_csize - 1));
        if (ult(btw, n))
            n = btw;
        if (off == 0 && n == SS)
            fr = window_noread(sect);
        else
            fr = move_window(sect);
        if (fr != FR_OK)
            return FR_DISK_ERR;
        for (i = 0; i < n; i++)
            win[off + i] = buf[i];
        windirty = 1;
        buf = buf + n;
        btw = btw - n;
        rw_n = rw_n + n;
        pos_add(fp, n);
        if (pos_lt_size(fp) == 0) {
            fp->slo = fp->plo;
            fp->shi = fp->phi;
        }
        fp->flag = fp->flag | 0x40;
    }
    return FR_OK;
}

int f_lseek2(struct fil *fp, unsigned lo, unsigned hi)
{
    unsigned slo;
    unsigned shi;
    unsigned big;
    big = ult(fp->shi, hi);
    if (hi == fp->shi)
        big = ult(fp->slo, lo);
    if (big) {
        if ((fp->flag & FA_WRITE) == 0) {
            lo = fp->slo;
            hi = fp->shi;
        } else {                                /* writing: grow the file (allocate the clusters) */
            slo = fp->plo;
            shi = fp->phi;
            if (lo || hi) {
                fp->plo = lo - 1;
                fp->phi = hi;
                if (lo == 0)
                    fp->phi = hi - 1;
                if (seek_clust(fp, 1) != FR_OK) {
                    fp->plo = slo;
                    fp->phi = shi;
                    return FR_DENIED;
                }
            }
            fp->slo = lo;
            fp->shi = hi;
            fp->flag = fp->flag | 0x40;
        }
    }
    fp->plo = lo;
    fp->phi = hi;
    if (fp->sclust) {
        fp->clust = fp->sclust;
        fp->cidx = 0;
    }
    return FR_OK;
}

char f_close(struct fil *fp)
{
    int fr;
    fr = sync_entry(fp);
    if (fr != FR_OK)
        return fr;
    return flush();
}

/* ---------------- directory operations ---------------- */
/* first cluster of the directory at path -> od_clust (0 = root) */
unsigned od_clust;
int opendir_clust(char *path)
{
    char *e;
    int fr;
    fr = walk(path);
    if (fr != FR_OK)
        return fr;
    if (w_isroot) {
        od_clust = 0;
        return FR_OK;
    }
    if (dir_find(w_dcl, w_nm) == 0)
        return FR_NO_PATH;
    e = dir_ptr(w_dcl, df_idx);
    if (e == 0 || (e[11] & AM_DIR) == 0)
        return FR_NO_PATH;
    od_clust = ld16(e + 26);
    return FR_OK;
}

int f_unlink(char *path)
{
    unsigned pdcl;
    unsigned idx;
    unsigned c;
    unsigned i;
    char *e;
    char *d;
    int fr;
    fr = walk(path);
    if (fr != FR_OK)
        return fr;
    if (w_isroot)
        return FR_INVALID_NAME;
    pdcl = w_dcl;
    if (dir_find(pdcl, w_nm) == 0)
        return FR_NO_FILE;
    idx = df_idx;
    e = dir_ptr(pdcl, idx);
    if (e == 0)
        return FR_DISK_ERR;
    if (e[11] & AM_RDO)
        return FR_DENIED;
    c = ld16(e + 26);
    if (e[11] & AM_DIR) {                       /* only an empty directory */
        i = 2;
        for (;;) {
            d = dir_ptr(c, i);
            if (d == 0 || d[0] == 0)
                break;
            if (d[0] != 0xE5 && d[11] != 0x0F)
                return FR_DENIED;
            i = i + 1;
        }
        e = dir_ptr(pdcl, idx);
        if (e == 0)
            return FR_DISK_ERR;
    }
    e[0] = 0xE5;
    windirty = 1;
    remove_chain(c);
    return flush();
}

char dotnm[11];
int f_mkdir(char *path)
{
    unsigned pdcl;
    unsigned c;
    unsigned i;
    char *e;
    int fr;
    fr = walk(path);
    if (fr != FR_OK)
        return fr;
    if (w_isroot)
        return FR_INVALID_NAME;
    pdcl = w_dcl;
    if (dir_find(pdcl, w_nm))
        return FR_EXIST;
    c = alloc_cluster(0);
    if (c == 0)
        return FR_DENIED;
    if (zero_cluster(c) != FR_OK)
        return FR_DISK_ERR;
    for (i = 0; i < 11; i++)
        dotnm[i] = ' ';
    dotnm[0] = '.';
    e = dir_ptr(c, 0);                          /* "." */
    if (e == 0)
        return FR_DISK_ERR;
    fill_entry(e, dotnm, AM_DIR, c);
    dotnm[1] = '.';
    e = dir_ptr(c, 1);                          /* ".." */
    if (e == 0)
        return FR_DISK_ERR;
    fill_entry(e, dotnm, AM_DIR, pdcl);
    fr = dir_alloc(pdcl);
    if (fr != FR_OK) {
        remove_chain(c);
        return fr;
    }
    e = dir_ptr(pdcl, da_idx);
    if (e == 0)
        return FR_DISK_ERR;
    fe_case = w_case;
    fill_entry(e, w_nm, AM_DIR, c);
    return flush();
}

char onm[11];
int f_rename(char *path_old, char *path_new)
{
    unsigned odcl;
    unsigned oidx;
    unsigned ndcl;
    unsigned i;
    char *e;
    int fr;
    fr = walk(path_old);
    if (fr != FR_OK)
        return fr;
    if (w_isroot)
        return FR_INVALID_NAME;
    odcl = w_dcl;
    if (dir_find(odcl, w_nm) == 0)
        return FR_NO_FILE;
    oidx = df_idx;
    e = dir_ptr(odcl, oidx);
    if (e == 0)
        return FR_DISK_ERR;
    for (i = 0; i < 32; i++)
        tmpent[i] = e[i];
    fr = walk(path_new);
    if (fr != FR_OK)
        return fr;
    if (w_isroot)
        return FR_INVALID_NAME;
    ndcl = w_dcl;
    if (dir_find(ndcl, w_nm))
        return FR_EXIST;
    fr = dir_alloc(ndcl);
    if (fr != FR_OK)
        return fr;
    e = dir_ptr(ndcl, da_idx);
    if (e == 0)
        return FR_DISK_ERR;
    for (i = 0; i < 32; i++)
        e[i] = tmpent[i];
    for (i = 0; i < 11; i++)
        e[i] = w_nm[i];
    e[12] = (e[12] & 0xE7) | w_case;            /* the new name's case flags */
    windirty = 1;
    e = dir_ptr(odcl, oidx);                    /* drop the old entry (the new one has another number) */
    if (e == 0)
        return FR_DISK_ERR;
    e[0] = 0xE5;
    windirty = 1;
    if ((tmpent[11] & AM_DIR) && odcl != ndcl) {        /* a moved directory: fix its ".." */
        e = dir_ptr(ld16(tmpent + 26), 1);
        if (e) {
            st16(e + 26, ndcl);
            windirty = 1;
        }
    }
    return flush();
}

/* free clusters, 0xFFFF = error */
unsigned free_clusters(void)
{
    unsigned c;
    unsigned n;
    unsigned v;
    n = 0;
    for (c = 2; c < v_nfatent; c++) {
        v = get_fat(c);
        if (v == 0xFFFF)
            return 0xFFFF;
        if (v == 0)
            n = n + 1;
    }
    return n;
}

/* ---------------- /dev ---------------- */
/* VFS_FAT for a FAT path, VFS_ENOENT for an unknown name under /dev, else DEV_* */
int vfs_resolve(char *p)
{
    char *q;
    if (p[0] != '/' || p[1] != 'd' || p[2] != 'e' || p[3] != 'v' || (p[4] != 0 && p[4] != '/'))
        return VFS_FAT;
    q = p + 4;
    while (*q == '/')
        q = q + 1;
    if (*q == 0)
        return DEV_DIR;
    if (q[0] == 'n' && q[1] == 'u' && q[2] == 'l' && q[3] == 'l' && (q[4] == 0 || q[4] == '/'))
        return DEV_NULL;
    if (q[0] == 'f' && q[1] == 'd' && (q[2] == 'a' || q[2] == 'b') && (q[3] == 0 || q[3] == '/')) {
        if (q[2] == 'a')
            return DEV_FDA;
        return DEV_FDB;
    }
    return VFS_ENOENT;
}

void vfs_init(void)
{
}

/* /dev/fda: raw read of the boot floppy from a byte position (sector granular buffer) */
unsigned fda_lo;
unsigned fda_hi;
char fda_sec[512];
void dev_open(int kind)
{
    fda_lo = 0;
    fda_hi = 0;
}

int dev_read(int kind, char *buf, unsigned n)
{
    unsigned off;
    unsigned i;
    unsigned o;
    if (kind != DEV_FDA)
        return 0;                               /* /dev/null: EOF */
    if (disk_read(0, fda_sec, (fda_hi << 7) | (fda_lo >> 9), 0, 1) != 0)
        return 0;
    off = fda_lo & 511;
    if (ult(512 - off, n))
        n = 512 - off;
    for (i = 0; i < n; i++)
        buf[i] = fda_sec[off + i];
    o = fda_lo;
    fda_lo = o + n;
    if (ult(fda_lo, o))
        fda_hi = fda_hi + 1;
    return n;
}

int dev_write(int kind, char *buf, unsigned n)
{
    if (kind == DEV_NULL)
        return n;
    return 0;                                   /* /dev/fda is read only */
}

/* ---------------- handles (fsbackend) ---------------- */
#define NFD 6
struct fil fdtab[NFD];
char fdused[NFD];

int map_fr(int r)
{
    if (r == FR_OK)
        return FSB_OK;
    if (r == FR_NO_FILE || r == FR_NO_PATH)
        return FSB_NOENT;
    if (r == FR_DENIED)
        return FSB_DENIED;
    return FSB_ERR;
}

int fsb_open(char *abspath, unsigned mode)
{
    unsigned i;
    unsigned m;
    int r;
    for (i = 0; i < NFD && fdused[i]; i++)
        ;
    if (i == NFD)
        return FSB_NOSPC;
    m = FA_READ;
    if (mode & 2) {                             /* FSB_WRITE */
        m = FA_WRITE;
        if (mode & 4)                           /* FSB_CREATE */
            m = FA_WRITE | FA_CREATE_ALWAYS;
        if (mode & 1)
            m = m | FA_READ;
    }
    r = f_open(&fdtab[i], abspath, m);
    if (r != FR_OK)
        return map_fr(r);
    fdused[i] = 1;
    return i;
}

int fsb_ok(int fh)
{
    if (fh < 0 || fh >= NFD)
        return 0;
    return fdused[fh];
}

int fsb_close(int fh)
{
    if (fsb_ok(fh) == 0)
        return FSB_ERR;
    f_close(&fdtab[fh]);
    fdused[fh] = 0;
    return FSB_OK;
}

char fsb_brw[2];
int fsb_read(int fh, char *buf, unsigned len)
{
    if (fsb_ok(fh) == 0)
        return 0;
    if (f_read(&fdtab[fh], buf, len, fsb_brw) != FR_OK)
        return 0;
    return rw_n;
}

int fsb_write(int fh, char *buf, unsigned len)
{
    if (fsb_ok(fh) == 0)
        return 0;
    if (f_write(&fdtab[fh], buf, len) != FR_OK)
        return 0;
    return rw_n;
}

/* lseek(fh, whence 0 = SET / 2 = END, offset hi:lo) -> new position low word; fsb_seek_hi() is the high word
 * (0xFFFF after an error, with 0xFFFF returned) */
unsigned seek_hi;
unsigned fsb_lseek(int fh, unsigned whence, unsigned lo, unsigned hi)
{
    struct fil *fp;
    unsigned o;
    if (fsb_ok(fh) == 0 || (whence != 0 && whence != 2)) {
        seek_hi = 0xFFFF;
        return 0xFFFF;
    }
    fp = &fdtab[fh];
    if (whence == 2) {
        o = lo;
        lo = lo + fp->slo;
        hi = hi + fp->shi;
        if (ult(lo, o))
            hi = hi + 1;
    }
    if (f_lseek2(fp, lo, hi) != FR_OK) {
        seek_hi = 0xFFFF;
        return 0xFFFF;
    }
    seek_hi = hi;
    return lo;
}

unsigned fsb_seek_hi(void)
{
    return seek_hi;
}

int fsb_unlink(char *abspath)
{
    return map_fr(f_unlink(abspath));
}

int fsb_mkdir(char *abspath)
{
    return map_fr(f_mkdir(abspath));
}

/* mv: the source must exist; an existing destination file (not a directory) is replaced */
int fsb_rename(char *src, char *dst)
{
    int r;
    if (f_open(&fdtab[NFD - 1], src, FA_READ) != FR_OK && opendir_clust(src) != FR_OK)
        return 1;
    r = map_fr(f_rename(src, dst));
    if (r == FSB_OK)
        return 0;
    if (f_open(&fdtab[NFD - 1], dst, FA_READ) == FR_OK && opendir_clust(dst) != FR_OK) {
        f_unlink(dst);
        r = map_fr(f_rename(src, dst));
        if (r == FSB_OK)
            return 0;
    }
    if (r == FSB_DENIED)
        return 0xFF;
    return 1;
}

/* df: 0 = total KB, 1 = free KB (512B sectors: KB = sectors / 2) */
unsigned kfs_df(unsigned sel)
{
    unsigned n;
    if (sel == 0)
        n = v_nfatent - 2;
    else {
        n = free_clusters();
        if (n == 0xFFFF)
            return 0xFFFF;
    }
    return (n << v_cshift) >> 1;
}

/* ---------------- directory iteration (kdir) ---------------- */
/* one at a time. kd_mode: 0 = closed, 1 = FAT, 2 = /dev; kd_dev: a FAT root listing ends with "dev" (1), /dev children */
char kd_mode;
char kd_dev;
unsigned kd_clust;
unsigned kd_idx;
unsigned kd_szlo;
unsigned kd_attr;

int kdir_open_abs(char *path)                   /* 0 = ok / -1 */
{
    int v;
    kd_mode = 0;
    v = vfs_resolve(path);
    if (v == VFS_FAT) {
        if (opendir_clust(path) != FR_OK)
            return -1;
        kd_clust = od_clust;
        kd_idx = 0;
        kd_dev = 0;
        if (path[1] == 0)
            kd_dev = 1;                         /* "/": add the synthetic "dev" after the FAT entries */
        kd_mode = 1;
        return 0;
    }
    if (v != DEV_DIR)
        return -1;
    kd_mode = 2;
    kd_dev = 0;
    return 0;
}

void kd_copy(char *name, char *s)
{
    unsigned i;
    for (i = 0; s[i]; i++)
        name[i] = s[i];
    name[i] = 0;
}

/* an upper-case letter of an 8.3 name shown in lower case when the entry's NT case flag says so */
unsigned kd_lc(unsigned c, unsigned flag)
{
    if (flag && c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
}

int kdir_read(char *name)                       /* 0 = end / 1 = file / 2 = directory */
{
    char *e;
    unsigned i;
    unsigned k;
    kd_szlo = 0;
    kd_attr = 0;
    name[0] = 0;
    if (kd_mode == 1) {
        for (;;) {
            e = dir_ptr(kd_clust, kd_idx);
            if (e == 0 || e[0] == 0)
                break;
            kd_idx = kd_idx + 1;
            if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 8) || e[11] == 0x0F)
                continue;
            k = 0;
            for (i = 0; i < 8 && e[i] != ' '; i++) {
                name[k] = kd_lc(e[i], e[12] & 8);
                k = k + 1;
            }
            if (e[8] != ' ') {
                name[k] = '.';
                k = k + 1;
                for (i = 8; i < 11 && e[i] != ' '; i++) {
                    name[k] = kd_lc(e[i], e[12] & 0x10);
                    k = k + 1;
                }
            }
            name[k] = 0;
            kd_attr = e[11];
            if (kd_attr & AM_DIR)
                return 2;
            kd_szlo = ld16(e + 28);
            return 1;
        }
        kd_mode = 0;
        if (kd_dev) {
            kd_dev = 0;
            kd_copy(name, "dev");
            return 2;
        }
        return 0;
    }
    if (kd_mode == 2) {
        kd_dev = kd_dev + 1;
        if (kd_dev == 1) {
            kd_copy(name, "null");
            return 1;
        }
        if (kd_dev == 2) {
            kd_copy(name, "fda");
            return 1;
        }
        if (kd_dev == 3) {
            kd_copy(name, "fdb");
            return 1;
        }
        kd_mode = 0;
    }
    return 0;
}

unsigned kdir_size(void)
{
    return kd_szlo;
}

void kdir_close(void)
{
    kd_mode = 0;
}

/* ---------------- cwd and path resolution ---------------- */
#define KPMAX 48
char kcwd[KPMAX];
char kpb0[KPMAX];
char kpb1[KPMAX];

/* base (absolute) + arg -> out (KPMAX); 0 = ok / -1 = too long */
int path_norm(char *base, char *arg, char *out)
{
    unsigned len;
    unsigned n;
    unsigned k;
    char *p;
    char *s;
    p = arg;
    while (*p == ' ')
        p = p + 1;
    if (*p == '/') {
        out[0] = '/';
        out[1] = 0;
        while (*p == '/')
            p = p + 1;
    } else {
        len = 0;
        while (base[len] && len < KPMAX - 1) {
            out[len] = base[len];
            len = len + 1;
        }
        out[len] = 0;
    }
    while (*p) {
        s = p;
        n = 0;
        while (*p && *p != '/') {
            p = p + 1;
            n = n + 1;
        }
        if (n == 1 && s[0] == '.') {
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            len = 0;
            while (out[len])
                len = len + 1;
            while (len > 0 && out[len - 1] != '/')
                len = len - 1;
            if (len > 1)
                len = len - 1;
            out[len] = 0;
        } else if (n > 0) {
            len = 0;
            while (out[len])
                len = len + 1;
            if (len + 1 + n > KPMAX - 1)
                return -1;
            if (len == 0 || out[len - 1] != '/') {
                out[len] = '/';
                len = len + 1;
            }
            for (k = 0; k < n; k++) {
                out[len] = s[k];
                len = len + 1;
            }
            out[len] = 0;
        }
        while (*p == '/')
            p = p + 1;
    }
    if (out[0] == 0) {
        out[0] = '/';
        out[1] = 0;
    }
    return 0;
}

/* in resolved against cwd into scratch slot 0 or 1; in itself if it does not fit */
char *kpath(char *in, char slot)
{
    char *buf;
    if (in == 0)
        return in;
    buf = kpb0;
    if (slot)
        buf = kpb1;
    if (path_norm(kcwd, in, buf) != 0)
        return in;
    return buf;
}

int kchdir(char *path)
{
    unsigned i;
    while (*path == ' ')
        path = path + 1;
    if (*path == 0)
        path = "/root";
    if (path_norm(kcwd, path, kpb0) != 0)
        return -1;
    if (kdir_open_abs(kpb0) != 0)
        return -1;
    kdir_close();
    for (i = 0; kpb0[i] && i < KPMAX - 1; i++)
        kcwd[i] = kpb0[i];
    kcwd[i] = 0;
    return 0;
}

void kgetcwd(char *out)
{
    unsigned i;
    for (i = 0; kcwd[i] && i < KPMAX - 1; i++)
        out[i] = kcwd[i];
    out[i] = 0;
}

int fat_init(void)
{
    kd_mode = 0;
    kd_copy(kcwd, "/root");
    return f_mount();
}

/* ---------------- redirection (> file, < file) ---------------- */
struct fil redir_fp;
char redir_active;
char redir_ch[2];

int redir_sink(int c)
{
    redir_ch[0] = c;
    f_write(&redir_fp, redir_ch, 1);
    return c;
}

int redir_begin(char *fname, char append)       /* 0 = ok, -1 = fail */
{
    int r;
    unsigned m;
    while (*fname == ' ')
        fname = fname + 1;
    fname = kpath(fname, 0);
    if (vfs_resolve(fname) != VFS_FAT) {
        kprintf("redir: %s: not a writable file\n", fname);
        return -1;
    }
    m = FA_WRITE | FA_CREATE_ALWAYS;
    if (append)
        m = FA_WRITE | FA_OPEN_APPEND;
    r = f_open(&redir_fp, fname, m);
    if (r) {
        kprintf("redir: %s: %d\n", fname, r);
        return -1;
    }
    redir_active = 1;
    redir_enable(1);
    return 0;
}

void redir_end(void)
{
    if (redir_active) {
        redir_enable(0);
        f_close(&redir_fp);
        redir_active = 0;
    }
}

struct fil in_fp;
char in_active;
char in_ch[2];

int in_src(void)                                /* -1 = EOF */
{
    if (f_read(&in_fp, in_ch, 1, fsb_brw) != FR_OK || rw_n == 0)
        return -1;
    return in_ch[0];
}

int in_begin(char *fname)
{
    int r;
    while (*fname == ' ')
        fname = fname + 1;
    fname = kpath(fname, 0);
    if (vfs_resolve(fname) != VFS_FAT) {
        kprintf("in: %s: not a readable file\n", fname);
        return -1;
    }
    r = f_open(&in_fp, fname, FA_READ);
    if (r) {
        kprintf("in: %s: %d\n", fname, r);
        return -1;
    }
    in_active = 1;
    in_enable(1);
    return 0;
}

void in_end(void)
{
    if (in_active) {
        in_enable(0);
        f_close(&in_fp);
        in_active = 0;
    }
}

/* ---------------- klog: append "YYYY-MM-DD HH:MM:SS [pid] msg" to /var/log/message ---------------- */
struct fil klog_fp;
char klog_pfx[26];
char klog_dim[12];

void klog_p2(unsigned at, unsigned n)
{
    unsigned d;
    d = 0;
    while (n >= 10) {
        n = n - 10;
        d = d + 1;
    }
    klog_pfx[at] = '0' + d;
    klog_pfx[at + 1] = '0' + n;
}

void klog_write(char *msg)
{
    unsigned lo;
    unsigned hi;
    unsigned days;
    unsigned rem;
    unsigned y;
    unsigned m;
    unsigned hh;
    unsigned mm;
    unsigned ydays;
    unsigned dim;
    unsigned len;
    char *kw;
    if (f_open(&klog_fp, "/var/log/message", FA_WRITE | FA_OPEN_APPEND) != FR_OK)
        return;
    klog_dim[0] = 31; klog_dim[1] = 28; klog_dim[2] = 31; klog_dim[3] = 30;
    klog_dim[4] = 31; klog_dim[5] = 30; klog_dim[6] = 31; klog_dim[7] = 31;
    klog_dim[8] = 30; klog_dim[9] = 31; klog_dim[10] = 30; klog_dim[11] = 31;
    lo = tz_kw_w(KW_EPOCH_O);
    hi = tz_kw_w(KW_EPOCH_O + 2);
    /* seconds -> days: 86400 = 0x1:5180; subtract while (hi:lo) >= 86400 */
    days = 0;
    while (hi > 1 || (hi == 1 && !ult(lo, 0x5180))) {
        if (ult(lo, 0x5180))
            hi = hi - 1;
        lo = lo - 0x5180;
        hi = hi - 1;
        days = days + 1;
    }
    rem = lo;                                   /* hi:lo < 86400 now */
    hh = 0;
    if (hi) {                                   /* 65536 s = 18 h + 736 s */
        hh = 18;
        rem = lo + 736;
    }
    while (!ult(rem, 3600)) {
        rem = rem - 3600;
        hh = hh + 1;
    }
    mm = 0;
    while (rem >= 60) {
        rem = rem - 60;
        mm = mm + 1;
    }
    y = 1970;
    for (;;) {
        ydays = 365;
        if ((y & 3) == 0)
            ydays = 366;
        if (ult(days, ydays))
            break;
        days = days - ydays;
        y = y + 1;
    }
    m = 1;
    while (m <= 12) {
        dim = klog_dim[m - 1];
        if (m == 2 && (y & 3) == 0)
            dim = 29;
        if (days < dim)
            break;
        days = days - dim;
        m = m + 1;
    }
    klog_p2(0, tz_udiv(y, 100));
    klog_p2(2, y - tz_udiv(y, 100) * 100);
    klog_pfx[4] = '-';
    klog_p2(5, m);
    klog_pfx[7] = '-';
    klog_p2(8, days + 1);
    klog_pfx[10] = ' ';
    klog_p2(11, hh);
    klog_pfx[13] = ':';
    klog_p2(14, mm);
    klog_pfx[16] = ':';
    klog_p2(17, rem);
    klog_pfx[19] = ' ';
    klog_pfx[20] = '[';
    kw = tz_kwork();
    klog_p2(21, kw[KW_CUR_O]);
    klog_pfx[23] = ']';
    klog_pfx[24] = ' ';
    f_write(&klog_fp, klog_pfx, 25);
    len = 0;
    while (msg && msg[len] && len < 230)
        len = len + 1;
    if (len)
        f_write(&klog_fp, msg, len);
    klog_pfx[0] = 10;
    f_write(&klog_fp, klog_pfx, 1);
    f_close(&klog_fp);
}

/* ---------------- whole-volume sync and raw boot-area sectors (sysup / reboot) ---------------- */
/* write back the directory entries of the open files and the window: nothing is lost by a reset after this */
void fs_sync_all(void)
{
    unsigned i;
    for (i = 0; i < NFD; i++)
        if (fdused[i])
            sync_entry(&fdtab[i]);
    if (redir_active)
        sync_entry(&redir_fp);
    flush();
}

/* one 512-byte sector of the boot drive, outside the FAT window (the window is written back first and dropped
 * after, so it never holds a stale copy). 0 = ok / -1 = error. The caller keeps lba inside the reserved area. */
int fs_rawread(unsigned lba, char *buf)
{
    if (flush() != FR_OK)
        return -1;
    winvalid = 0;
    if (disk_read(0, buf, lba, 0, 1) != 0)
        return -1;
    return 0;
}

int fs_rawwrite(unsigned lba, char *buf)
{
    if (flush() != FR_OK)
        return -1;
    winvalid = 0;
    if (disk_write(0, buf, lba, 0, 1) != 0)
        return -1;
    return 0;
}
