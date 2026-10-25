#include "kw.h"
/* tizix x86-i386-unreal : kernel entry (called from crt0.s) -- tzcc version.
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
unsigned tz_bss_end(void);
unsigned tz_sp(void);
void tz_set_nslot_rt(unsigned n);
void tz_halt(void);
void tz_set_lowword(unsigned off, unsigned v);
unsigned x86_cpu_type(void);
unsigned xmem_init(void);
void xmem_write(unsigned lo, unsigned hi, char *src, unsigned n);
void xmem_read(unsigned lo, unsigned hi, char *dst, unsigned n);

char numbuf[8];
unsigned kstk_lo;

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

/* syscall stack watermark: fill at boot, then measure the untouched run at the bottom of each slot */
void scstk_fill(void)
{
    unsigned i;
    unsigned n;
    char *p;
    p = tz_scstk_base();
    n = MAXSLOT * SCSTK;
    for (i = 0; i < n; i++)
        p[i] = 0xA5;
}

unsigned x86_scstk_headroom(void)
{
    unsigned k;
    unsigned i;
    unsigned best;
    char *b;
    best = SCSTK;
    for (k = 0; k < MAXSLOT; k++) {
        b = tz_scstk_base() + k * SCSTK;
        i = 0;
        while (i < SCSTK && b[i] == 0xA5)
            i++;
        if (i < best)
            best = i;
    }
    return best;
}

/* kernel stack watermark: fill from the end of bss up to just below the current SP */
void kstk_fill(void)
{
    unsigned i;
    unsigned n;
    char *p;
    kstk_lo = tz_bss_end();
    p = kstk_lo;
    n = (tz_sp() - kstk_lo - 96) >> 1;
    for (i = 0; i < n; i++) {
        p[i * 2] = 0xA5;
        p[i * 2 + 1] = 0xA5;
    }
}

unsigned x86_kstack_headroom(void)
{
    unsigned i;
    unsigned lim;
    char *p;
    p = kstk_lo;
    lim = (0xFF00 - kstk_lo) >> 1;
    i = 0;
    while (i < lim && p[i * 2] == 0xA5 && p[i * 2 + 1] == 0xA5)
        i++;
    return i * 2;
}

/* x86-i386-unreal: unreal mode (xmem.s) round trip, 0 = not usable / 1 = extended memory read back what was written */
char xm_pat[16];
char xm_back[16];
unsigned xm_ok;

unsigned x86_xmem_status(void)
{
    return xm_ok;
}

void xmem_selftest(unsigned ext_hi, unsigned ext_lo)
{
    unsigned i;
    unsigned hi;
    if (xmem_init() == 0)
        return;
    /* where to test: 2MB if there are 3MB or more of extended memory, 1MB + 64KB if there are 128KB .. 3MB */
    if (ext_hi || ext_lo >= 3072)
        hi = 0x0020;
    else if (ext_lo >= 128)
        hi = 0x0011;
    else
        return;
    for (i = 0; i < 16; i++) {
        xm_pat[i] = (i * 37 + 0x55) ^ (i << 4);
        xm_back[i] = 0;
    }
    xmem_write(0, hi, xm_pat, 16);
    xmem_read(0, hi, xm_back, 16);
    for (i = 0; i < 16; i++)
        if (xm_back[i] != xm_pat[i])
            return;
    xm_ok = 1;
}

/* minimum (TK 2026-10-24): a 386 or later and 4MB of memory. A 4MB machine does not always report 3072KB of extended
 * memory (QEMU -m 4: 2944KB, the BIOS keeps the top 128KB), a 3MB machine reports 2048KB at most: the floor is 2560KB. */
#define MIN_EXT_KB 2560

void halt_with(char *why)
{
    con_puts("tizix x86-i386-unreal needs ");
    con_puts(why);
    con_puts(": halted\n");
    for (;;)
        tz_halt();
}

void kmain(void)
{
    unsigned ext_lo;
    unsigned ext_hi;
    unsigned n;
    unsigned cpu;

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

    cpu = x86_cpu_type();
    if (cpu >= 3)
        xmem_selftest(ext_hi, ext_lo);
    con_puts("cpu: ");
    if (cpu >= 3)
        con_puts("386+");
    else if (cpu == 2)
        con_puts("286");
    else
        con_puts("8086/186");
    con_puts(", unreal mode: ");
    if (xm_ok)
        con_puts("ok");
    else if (cpu < 3)
        con_puts("n/a");
    else if (ext_hi == 0 && ext_lo < 128)
        con_puts("no extended memory");
    else
        con_puts("failed");
    con_puts("\n");

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
    con_puts("\n");
    if (cpu < 3)
        halt_with("a 386 or later CPU");
    if (ext_hi == 0 && ext_lo < MIN_EXT_KB)
        halt_with("4MB of memory or more");

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
