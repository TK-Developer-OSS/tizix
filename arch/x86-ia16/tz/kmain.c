#include "kw.h"
/* tizix x86-ia16 : kernel entry (called from crt0.s) -- tzcc version.
 *
 * Written to the C subset that tzcc --march=ia16 compiles as-is:
 *   - no 32-bit values, no inline asm, no data shared with other objects
 *     (the asm side is reached through the tz_* functions in tz/glue.s)
 *   - tzcc reads char as unsigned (zero-extended), so compare bytes against 0xA5, not -91
 *   - 16-bit values may be >= 0x8000, but tzcc compares and divides them as signed,
 *     so counts are kept below 0x8000 and halves are taken with >> (logical shift).
 */
void con_init(void);
void con_puts(const char *s);
void init(void);
unsigned x86_nslots(void);
unsigned x86_lowword(unsigned off);
unsigned x86_pool_blocks(void);
unsigned tz_scstk_base(void);
unsigned tz_sp(void);
int tz_peekb(unsigned seg, unsigned off);
void tz_pokeb(unsigned seg, unsigned off, unsigned val);
void tz_set_nslot_rt(unsigned n);
void tz_halt(void);
void tz_set_lowword(unsigned off, unsigned v);

char numbuf[8];
unsigned kstk_lo;
unsigned kstk_n;                /* words filled by kstk_fill */

/* decimal print of a 16-bit unsigned value; v/10 is taken as (v>>1)/5 so v >= 0x8000 works */
void put_u16(unsigned v)
{
    int i;
    unsigned q;
    i = 6;
    numbuf[7] = 0;
    numbuf[6] = 0;
    for (;;) {
        q = (v >> 1) / 5;
        i = i - 1;
        numbuf[i] = '0' + (v - q * 10);
        v = q;
        if (v == 0) break;
    }
    con_puts(numbuf + i);
}

/* file system stack watermark (fs_stack of glue.S, FSSTK bytes; reported as "syscall stack headroom": syscalls run on the
 * caller's stack, the only stack the kernel has besides the boot one is the one the FatFs code runs on). */
#define FSSTK 1280

void scstk_fill(void)
{
    unsigned i;
    char *p;
    p = tz_scstk_base();
    for (i = 0; i < FSSTK; i++)
        p[i] = 0xA5;
}

unsigned x86_scstk_headroom(void)
{
    unsigned i;
    char *b;
    b = tz_scstk_base();
    i = 0;
    while (i < FSSTK && b[i] == 0xA5)
        i++;
    return i;
}

/* kernel stack watermark: the stack is in segment 0 (physical 0x600 up to the SP); fill from 0x600 to just below the SP */
void kstk_fill(void)
{
    unsigned i;
    kstk_lo = 0x600;
    kstk_n = tz_sp() - 96 - kstk_lo;
    for (i = 0; i < kstk_n; i++)
        tz_pokeb(0, kstk_lo + i, 0xA5);
}

unsigned x86_kstack_headroom(void)
{
    unsigned i;
    i = 0;
    while (i < kstk_n && tz_peekb(0, kstk_lo + i) == 0xA5)
        i++;
    return i;
}

void kmain(void)
{
    unsigned ext_lo;
    unsigned ext_hi;
    unsigned n;

    con_init();
    if (MEMCAP_KB) {                    /* software memory limit: the pool, free and the banner all read 0000:0512 */
        ext_lo = x86_lowword(0x512);
        if (ext_lo > MEMCAP_KB)
            tz_set_lowword(0x512, MEMCAP_KB);
    }
    scstk_fill();
    kstk_fill();

    ext_lo = x86_lowword(0x516);        /* E801: KB between 1MB and 16MB */
    ext_hi = x86_lowword(0x518);        /* E801: 64KB units above 16MB */
    if (ext_lo == 0 && ext_hi == 0)
        ext_lo = x86_lowword(0x514);    /* INT 15h/88h */
    con_puts("memory: conventional ");
    put_u16(x86_lowword(0x512));
    con_puts("KB, extended ");
    if (ext_hi == 0) {
        put_u16(ext_lo);
        con_puts("KB");
    } else {
        put_u16((ext_lo >> 10) + (ext_hi >> 4));    /* 32-bit KB does not fit; show MB */
        con_puts("MB");
    }
    con_puts(" (unused)\n");

    n = x86_nslots();
    tz_set_nslot_rt(n);
    con_puts("blocks ");
    put_u16(x86_pool_blocks());
    con_puts(" x 4KB, slots ");
    put_u16(n);
    con_puts("\n");
    init();

    for (;;)
        tz_halt();
}
