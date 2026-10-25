/* tizix x86-ia16 : program loader and the 4KB block pool -- tzcc version of ../../src/kexec.c (x86 branch).
 *
 * Pool layout (differs from the gcc version, which starts at segment 0x2000 and so wastes the low 64KB):
 *   - blocks at segments 0x0100 .. 0x0F00 (physical 0x1000..0xFFFF): the area stage1 (0x7C00) and stage2 (0x8000) used
 *     to load the kernel. They are dead once the kernel runs, and the IVT / BDA / boot info sit below 0x1000. 15 blocks.
 *   - the kernel's own 64KB window at segment 0x1000..0x1F00 is skipped,
 *   - blocks from segment 0x2000 up to the end of conventional memory.
 *   A run of blocks must be physically contiguous, so an allocation never spans the gap after block 14.
 *   Blocks are numbered p = 0.. over the pool; pool_seg_hi(p) is the high byte of the block's segment.
 *   x86_slot_seg[] / x86_slot_blk[] are defined in glue.S (crt0.s reads x86_slot_seg), reached through tz_seg_* / tz_blk_*.
 *
 * tzcc compares 16-bit values as signed, so every value that can be >= 0x8000 (segments, byte counts) is
 * taken apart with >> before it is compared: a segment is handled as its high byte.
 * Functions returning a narrow type (FRESULT, char) leave AH undefined: declare them `char` and tzcc clears AH after the call.
 */
#include "kw.h"

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

char kx_fil[32];            /* struct fil of tz/fs.c (20 bytes) */
char kx_bounce[256];
unsigned kx_br;
unsigned kx_ctx[12];
char kx_arg[64];
char kx_joined[128];
unsigned kx_total;
char kx_have_total;

#define POOL_LOW 15                             /* blocks below the kernel window: segments 0x0100 .. 0x0F00 */

/* high byte of the segment of pool block p / pool block of a segment's high byte */
unsigned pool_seg_hi(unsigned p)
{
    if (p < POOL_LOW)
        return p + 1;
    return p + 0x11;                            /* p = 15 is segment 0x2000 */
}

unsigned pool_idx(unsigned seg_hi)
{
    if (seg_hi < 0x10)
        return seg_hi - 1;
    return seg_hi - 0x11;
}

/* number of 4KB blocks in the pool: the low blocks, then everything from segment 0x2000 to the end of conventional memory */
unsigned x86_pool_blocks(void)
{
    unsigned kb;
    unsigned top;
    unsigned low;

    if (!kx_have_total) {
        kb = x86_lowword(0x512);
        if (kb == 0) kb = 640;
        top = ((kb * 64) & 0xFF00) >> 8;        /* end of memory as a 4KB-aligned high byte of the segment */
        low = POOL_LOW;
        if (top < 0x10)
            low = top - 1;                      /* less than 64KB of memory: fewer low blocks (no kernel window then) */
        kx_total = low;
        if (top > 0x20)
            kx_total = low + top - 0x20;
        kx_have_total = 1;
    }
    return kx_total;
}

/* 1 if block b is owned by a live slot */
unsigned blk_used(unsigned b)
{
    char *kw;
    unsigned k;
    unsigned s;
    unsigned ns;

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
        if (b == POOL_LOW)
            run = 0;                            /* the kernel window lies between block 14 and block 15 */
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
        if (b == POOL_LOW)
            run = 0;                            /* not contiguous across the kernel window */
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
    nblk = (bssend + 0x800 + 0xFFF) >> 12;
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

char kload_driver(void)
{
    return 0;
}
