/* tizix x86-ia16 : program loader and the 4KB block pool -- tzcc version of ../../src/kexec.c (x86 branch).
 *
 * Pool layout (differs from the gcc version, which starts at segment 0x2000 and so wastes the low 64KB):
 *   the kernel sits at physical 0x1000 (KSEG 0x0100: text, data, bss), its stack in segment 0 below it (physical 0x600..0x1000,
 *   see link-kernel.ld), and the pool is every 4KB block from the first one after the kernel up to the end of conventional
 *   memory, without a gap. stage1 (0x7C00) and stage2 (moved to the last 4KB by itself) are dead once the kernel runs, so
 *   they are pool too.
 *   Blocks are numbered p = 0.. over the pool; pool_seg_hi(p) is the high byte of the block's segment.
 *   x86_slot_seg[] / x86_slot_blk[] are defined in glue.S (crt0.s reads x86_slot_seg), reached through tz_seg_* / tz_blk_*.
 *
 * tzcc compares 16-bit values as signed, so every value that can be >= 0x8000 (segments, byte counts) is
 * taken apart with >> before it is compared: a segment is handled as its high byte.
 * Functions returning a narrow type (FRESULT, char) leave AH undefined: declare them `char` and tzcc clears AH after the call.
 */
#include "kw.h"
#include "fsx.h"
#include "user/drvpsp.h"

char *tz_kwork(void);
unsigned tz_kw_w(unsigned off);
void tz_kw_setw(unsigned off, unsigned v);
unsigned tz_seg_get(unsigned k);
void tz_seg_set(unsigned k, unsigned v);
unsigned tz_blk_get(unsigned k);
void tz_blk_set(unsigned k, unsigned v);
unsigned x86_lowword(unsigned off);
unsigned x86_nslots(void);
void farcpy(unsigned dseg, unsigned doff, char *src, unsigned n);
char f_open(char *fp, char *path, unsigned mode);       /* FRESULT: returned in AL only; tzcc clears AH after a call to a char function */
char f_close(char *fp);
char f_read(char *fp, char *buf, unsigned btr, unsigned *br);
unsigned pipe_bufseg(void);
unsigned tz_pool_base(void);
void redir_note_child(char n);

char kx_fil[40];            /* FIL of FatFs (34 bytes with FF_FS_TINY) */
char kx_bounce[256];
unsigned kx_br;
unsigned kx_ctx[12];
char kx_arg[64];
char kx_joined[128];
unsigned kx_total;
char kx_have_total;

unsigned kx_base;                               /* high byte of the segment of pool block 0 */
unsigned kx_drvseg;                             /* the resident DRIVER (/bin/driver.bin): its segment, 0 = none */
unsigned kx_drvblk;                             /* and its blocks, which blk_used counts as taken */
char kx_zero[16];                               /* never written: zeros for the process area */
#define DRV_MAXBLK 2
#define PSTK 0x800                              /* least stack kept above bss in every process (4 x the deepest measured, about 500B) */

/* high byte of the segment of pool block p / pool block of a segment's high byte */
unsigned pool_seg_hi(unsigned p)
{
    return kx_base + p;
}

unsigned pool_idx(unsigned seg_hi)
{
    return seg_hi - kx_base;
}

/* number of 4KB blocks in the pool: from the first block after the kernel to the end of conventional memory */
unsigned x86_pool_blocks(void)
{
    unsigned kb;
    unsigned top;

    if (!kx_have_total) {
        kx_base = tz_pool_base();
        kb = x86_lowword(0x512);
        if (kb == 0) kb = 640;
        top = ((kb * 64) & 0xFF00) >> 8;        /* end of memory as a 4KB-aligned high byte of the segment */
        kx_total = 0;
        if (top > kx_base)
            kx_total = top - kx_base;
        kx_have_total = 1;
    }
    return kx_total;
}

/* 1 if block b is owned by a live slot or holds the pipe buffer */
unsigned blk_used(unsigned b)
{
    char *kw;
    unsigned k;
    unsigned s;
    unsigned ns;

    s = pipe_bufseg();
    if (s && b == pool_idx(s >> 8))
        return 1;
    if (kx_drvblk) {
        s = pool_idx(kx_drvseg >> 8);
        if (b >= s && b < s + kx_drvblk)
            return 1;
    }
    kw = tz_kwork();
    ns = x86_nslots();
    for (k = 1; k < ns; k++) {
        if (kw[k] == 0 || tz_blk_get(k) == 0)
            continue;
        s = pool_idx(tz_seg_get(k) >> 8);       /* first block of the slot */
        if (b >= s && b < s + tz_blk_get(k))
            return 1;
    }
    return 0;
}

/* first run of `need` free blocks; returns its segment, 0 if none */
unsigned blk_alloc(unsigned need)
{
    unsigned total;
    unsigned run;
    unsigned b;
    unsigned s;

    total = x86_pool_blocks();
    run = 0;
    for (b = 0; b < total; b++) {
        if (blk_used(b)) {
            run = 0;
            continue;
        }
        run = run + 1;
        if (run == need) {
            s = b + 1 - need;
            return pool_seg_hi(s) << 8;
        }
    }
    return 0;
}

/* sel: 0 total, 1 used, 2 free, 3 largest free run */
unsigned x86_blkinfo(unsigned sel)
{
    unsigned total;
    unsigned b;
    unsigned used;
    unsigned run;
    unsigned best;

    total = x86_pool_blocks();
    used = 0;
    run = 0;
    best = 0;
    for (b = 0; b < total; b++) {
        if (blk_used(b)) {
            used = used + 1;
            run = 0;
        } else {
            run = run + 1;
            if (run > best) best = run;
        }
    }
    if (sel == 0) return total;
    if (sel == 1) return used;
    if (sel == 2) return total - used;
    return best;
}

/* load fname into a free slot and start it; returns the slot number, 0 if none is free or it does not fit, 0xFF if it cannot be opened */
char kexec_file(char *fname, char *arg)
{
    char *kw;
    unsigned n;
    unsigned ns;
    unsigned pseg;
    unsigned off;
    unsigned i;
    unsigned nblk;
    unsigned csp;
    unsigned bssend;
    unsigned hi;
    unsigned lo;
    unsigned bad;
    unsigned xw;
    unsigned xb;

    kw = tz_kwork();
    if (f_open(kx_fil, fname, 1) != 0)          /* FA_READ */
        return 0xFF;
    ns = x86_nslots();
    for (n = 1; n < ns; n++)
        if (kw[n] == 0)
            break;
    if (n >= ns) {
        f_close(kx_fil);
        return 0;
    }
    if (f_read(kx_fil, kx_bounce, 256, &kx_br) != 0 || kx_br < 4) {
        f_close(kx_fil);
        return 0xFF;
    }

    /* the .BIN header word at +2 is its __bss_end; size the allocation from it (+2KB stack) */
    bssend = kx_bounce[2] | (kx_bounce[3] << 8);
    hi = bssend >> 8;
    lo = bssend & 0xFF;
    bad = 0;
    if (hi == 0) bad = 1;                       /* bssend < 0x104 */
    if (hi == 1 && lo < 4) bad = 1;
    if (hi > 0xE0) bad = 1;                     /* bssend > 0xE000 */
    if (hi == 0xE0 && lo != 0) bad = 1;
    if (bad) bssend = 0xF000;
    /* minimum = bss + PSTK of stack. Syscalls run on this stack too (SC_PSTACK); measured at most about 500B (sh). */
    nblk = (bssend + PSTK + 0xFFF) >> 12;
    pseg = 0;
    xw = kx_bounce[4] | (kx_bounce[5] << 8);    /* __xwant: bytes wanted above the minimum, given if there is room */
    if (xw && kx_br >= 6 && (xw >> 8) < 0x80) {
        xb = (xw + 0xFFF) >> 12;
        pseg = blk_alloc(nblk + xb);
        if (pseg)
            nblk = nblk + xb;
    }
    if (!pseg)
        pseg = blk_alloc(nblk);
    if (!pseg) {
        f_close(kx_fil);
        return 0;
    }

    farcpy(pseg, 0x0100, kx_bounce, kx_br);     /* .COM style: image at pseg:0100 */
    off = 0x0100 + kx_br;
    for (;;) {
        if (f_read(kx_fil, kx_bounce, 256, &kx_br) != 0 || kx_br == 0)
            break;
        farcpy(pseg, off, kx_bounce, kx_br);
        off = off + kx_br;
    }
    f_close(kx_fil);

    for (i = 0; i < DRV_PSP_END; i = i + 16)    /* the process area (user/drvpsp.h): zeroed, then the DRIVER */
        farcpy(pseg, i, kx_zero, 16);
    kx_ctx[0] = kx_drvseg;
    farcpy(pseg, DRV_PSP_SEG, kx_ctx, 2);

    i = 0;                                      /* argument string at pseg:0080 */
    if (arg) {
        while (arg[i] && i < 63) {
            kx_arg[i] = arg[i];
            i = i + 1;
        }
    }
    kx_arg[i] = 0;
    farcpy(pseg, 0x0080, kx_arg, i + 1);

    /* fake context, matching the restore sequence of _isr08:  [es][ds][bp][di][si][dx][cx][bx][ax][IP][CS][FLAGS] */
    kx_ctx[0] = pseg;
    kx_ctx[1] = pseg;
    for (i = 2; i < 9; i++)
        kx_ctx[i] = 0;
    kx_ctx[9] = 0x0100;
    kx_ctx[10] = pseg;
    kx_ctx[11] = 0x0202;                        /* IF=1 */
    csp = (nblk << 12) - 0x10 - 24;
    farcpy(pseg, csp, kx_ctx, 24);

    redir_note_child(n);                        /* a redirected foreground command shares sh's "> file" / "< file" */
    tz_seg_set(n, pseg);
    tz_blk_set(n, nblk);
    tz_kw_setw(KW_SP_O + n * 2, csp);
    tz_kw_setw(KW_SS_O + n * 2, pseg);
    kw[n] = n;                                  /* last: the scheduler may run it from now on */
    return n;
}

/* join an argv pack (NUL separated, argc strings) with spaces and exec */
char kexec_argv(char *fname, char *argpack, char argc)
{
    unsigned o;
    unsigned k;

    o = 0;
    for (k = 0; k < argc; k++) {
        if (k && o < 127) {
            kx_joined[o] = ' ';
            o = o + 1;
        }
        while (*argpack) {
            if (o < 127) {
                kx_joined[o] = *argpack;
                o = o + 1;
            }
            argpack = argpack + 1;
        }
        argpack = argpack + 1;
    }
    kx_joined[o] = 0;
    return kexec_file(fname, kx_joined);
}

/* /bin/driver.bin into the pool, resident (as DRIVER.BIN in block1 on the z80, ../../src/kexec.c): the stdio
 * every command calls through user/drvc.S. It is code only, at offset 0 of its segment, no relocation.
 * DRV_MAXBLK blocks are taken while it loads, then the unused ones are given back.
 * 0 = loaded, 0xFF = missing / empty / too big (builtin_init stops: commands cannot run without it). */
char kload_driver(void)
{
    unsigned seg;
    unsigned off;

    if (f_open(kx_fil, "/bin/driver.bin", 1) != 0)
        return 0xFF;
    seg = blk_alloc(DRV_MAXBLK);
    if (!seg) {
        f_close(kx_fil);
        return 0xFF;
    }
    kx_drvseg = seg;
    kx_drvblk = DRV_MAXBLK;
    off = 0;
    for (;;) {
        if (f_read(kx_fil, kx_bounce, 256, &kx_br) != 0 || kx_br == 0)
            break;
        if (off + kx_br > DRV_MAXBLK * 4096) {
            off = 0;                            /* too big */
            break;
        }
        farcpy(seg, off, kx_bounce, kx_br);
        off = off + kx_br;
    }
    f_close(kx_fil);
    if (off == 0) {
        kx_drvseg = 0;
        kx_drvblk = 0;
        return 0xFF;
    }
    kx_drvblk = (off + 0xFFF) >> 12;
    return 0;
}
