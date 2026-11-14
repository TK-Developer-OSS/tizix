#include "console.h"

/* ------------------------------------------------------------------
 * Layer 1 for MSX2: VDP text screen (SCREEN 0, 80 columns, set up by s2.s
 * through the BIOS) and the keyboard matrix.
 *
 *   Screen: the name table is written straight through the VDP ports.  Its address
 *   and the size come from the BIOS variables NAMBAS / LINLEN / CRTCNT, which
 *   block 7 keeps intact.  The VDP address latch is two writes to port 0x99; the
 *   vblank ISR reads the status register (also port 0x99), which resets that latch,
 *   so every address setup and its data run with interrupts off.
 *
 *   Keyboard: matrix rows 0-8 (port 0xAA selects the row, 0xA9 returns it, a pressed
 *   key reads 0), scanned from the vblank ISR.  No key repeat, no kana, no function keys.
 *
 *   Not done: a visible cursor, key repeat, 50Hz machines (TICK_HZ is 60).
 * ------------------------------------------------------------------ */
__sfr __at 0x98 VDATA;
__sfr __at 0x99 VCTRL;
__sfr __at 0xA9 PPIB;
__sfr __at 0xAA PPIC;

#define NAMBAS  (*(volatile unsigned *)0xF922)
#define LINLEN  (*(volatile unsigned char *)0xF3B0)
#define CRTCNT  (*(volatile unsigned char *)0xF3B1)

static unsigned char cols, rows, ccol, crow, ready;
static unsigned char in_esc, par[2], npar;

/* interrupt flag save / restore (returns 1 when they were on) */
static unsigned char irq_save(void) __naked
{
    __asm
        ld      a, i
        jp      pe, 1$
        ld      a, i
1$:     di
        ld      l, #0
        jp      po, 2$
        ld      l, #1
2$:     ret
    __endasm;
}

static void irq_restore(unsigned char f) __naked
{
    f;
    __asm
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        or      a
        ret     z
        ei
        ret
    __endasm;
}

/* VDP address setup: w = 0x40 for writing, 0 for reading.  Interrupts must be off. */
static void vaddr(unsigned a, unsigned char w)
{
    VCTRL = (unsigned char)a;
    VCTRL = (unsigned char)((a >> 8) & 0x3F) | w;
}

/* fill n cells from address a with ' ' */
static void vclear(unsigned a, unsigned n)
{
    vaddr(a, 0x40);
    while (n--) VDATA = ' ';
}

/* all rows up by one, then clear the last.  Cell by cell with no row buffer: block 0 has no
 * room for one (the C statics must stay below KW_CMDNAME, 0x830B).  Interrupts are let
 * through between rows (irqf = the state con_putc found), so the vblank ISR (keyboard scan,
 * tick) is not held off for long.
 *   Assembly, registers only (~170 T-states a cell, ~4ms a row).  The C version called
 *   vaddr() twice a cell and took ~1s a scroll with interrupts off ~40ms at a time; a key
 *   pressed and released inside such a window was never seen (typing right after Enter
 *   on a full screen lost keys). */
static unsigned char irqf;

static void scroll(void) __naked
{
    __asm
        ld      de, (0xF922)          ; d = NAMBAS
        ld      a, (_cols)
        ld      l, a
        ld      h, #0
        add     hl, de                ; s = NAMBAS + cols
        ld      a, (_rows)
        dec     a
        ld      c, a                  ; rows - 1 rows to move
1$:     ld      a, (_cols)
        ld      b, a
2$:     ld      a, l                  ; read address s
        out     (0x99), a
        ld      a, h
        and     #0x3F
        out     (0x99), a
        inc     hl
        in      a, (0x98)
        push    af
        ld      a, e                  ; write address d
        out     (0x99), a
        ld      a, d
        and     #0x3F
        or      #0x40
        out     (0x99), a
        pop     af
        out     (0x98), a
        inc     de
        djnz    2$
        ld      a, (_irqf)            ; a pending vblank ISR runs here
        or      a
        jr      z, 3$
        ei
        nop
        di
3$:     dec     c
        jr      nz, 1$
        ld      a, e                  ; clear the last row (d now points at it)
        out     (0x99), a
        ld      a, d
        and     #0x3F
        or      #0x40
        out     (0x99), a
        ld      a, (_cols)
        ld      b, a
        ld      a, #0x20
4$:     out     (0x98), a
        djnz    4$
        ret
    __endasm;
}

static void newline(void)
{
    if (++crow >= rows) {
        crow = rows - 1;
        scroll();
    }
}

static void putc_irqoff(unsigned char c)
{
    if (!ready) {
        cols = LINLEN;
        rows = CRTCNT;
        if (cols < 40 || cols > 132) cols = 80;
        if (rows < 20 || rows > 26) rows = 24;
        ready = 1;
        vclear(NAMBAS, (unsigned)cols * rows);
    }

    if (in_esc == 2) {                       /* ESC [ params final */
        if (c >= '0' && c <= '9') par[npar] = par[npar] * 10 + (c - '0');
        else if (c == ';') npar = 1;
        else {
            in_esc = 0;
            if (c == 'H') {                  /* ESC [ row ; col H  (1-based, 0 or none = 1) */
                crow = par[0] ? par[0] - 1 : 0;
                ccol = par[1] ? par[1] - 1 : 0;
            } else if (c == 'J') {           /* ESC [ 2 J: clear the screen */
                vclear(NAMBAS, (unsigned)cols * rows);
                ccol = crow = 0;
            } else if (c == 'K') {           /* ESC [ K: clear to the end of the line */
                vclear(NAMBAS + (unsigned)crow * cols + ccol, cols - ccol);
            }
        }
        return;
    }
    if (in_esc == 1) {
        in_esc = c == '[' ? 2 : 0;
        par[0] = par[1] = npar = 0;
        if (in_esc) return;
    }
    if (c == 27) in_esc = 1;
    else if (c == 13) ccol = 0;
    else if (c == 10) newline();
    else if (c == 8) { if (ccol) ccol--; }
    else if (c >= 32) {
        vaddr(NAMBAS + (unsigned)crow * cols + ccol, 0x40);
        VDATA = c;
        if (++ccol >= cols) { ccol = 0; newline(); }
    }
}

void con_putc(char c)
{
    unsigned char f = irq_save();
    irqf = f;
    putc_irqoff((unsigned char)c);
    irq_restore(f);
}

/* ------------------------------------------------------------------
 * keyboard: matrix row r bit b -> kt[r * 8 + b] (US layout; 0 = no character), kts[] with SHIFT.
 *   row 0: 0-7      row 1: 8 9 - = \ [ ] ;     row 2: ' ` , . / (dead) a b     rows 3-5: c..z
 *   row 6: SHIFT CTRL GRAPH CAPS CODE F1-F3     row 7: F4 F5 ESC TAB STOP BS SELECT RETURN
 *   row 8: SPACE HOME INS DEL LEFT UP DOWN RIGHT          (STOP is Ctrl+C)
 *   The cursor keys are 0x80 | 'D' 'A' 'B' 'C' in kt[] and go into the queue as
 *   ESC [ D/A/B/C, the sequences vi (and the ia16 sh history) read from a terminal.
 * ------------------------------------------------------------------ */
static const char kt[72] =
    "01234567" "89-=\\[];" "'`,./\0ab" "cdefghij" "klmnopqr" "stuvwxyz"
    "\0\0\0\0\0\0\0\0" "\0\0\x1b\x09\x03\x08\0\x0d" " \0\0\x7f\xC4\xC1\xC2\xC3";
static const char ks[48] =
    ")!@#$%^&" "(*_+|{}:" "\"~<>?\0AB" "CDEFGHIJ" "KLMNOPQR" "STUVWXYZ";

static unsigned char kprev[9];
static unsigned char kq[32];
static volatile unsigned char kqh, kqt;

static unsigned char krow(unsigned char r)
{
    PPIC = (PPIC & 0xF0) | r;
    return PPIB;
}

/* one byte into the key queue (dropped when full; 0 is no key) */
static void kput(unsigned char k)
{
    if (k && ((kqh + 1) & 31) != kqt) { kq[kqh] = k; kqh = (kqh + 1) & 31; }
}

void kb_isr(void)
{
    unsigned char r, b, i, m, k, sh, ctl;

    r = krow(6);
    sh = !(r & 1);
    ctl = !(r & 2);
    for (r = 0; r < 9; r++) {
        b = krow(r);
        m = (unsigned char)(kprev[r] & ~b);       /* released -> pressed */
        kprev[r] = b;
        for (i = 0; m; i++, m >>= 1) {
            if (!(m & 1)) continue;
            k = kt[r * 8 + i];
            if (sh && r < 6) k = ks[r * 8 + i];
            else if (ctl && k >= 'a' && k <= 'z') k &= 0x1F;
            if (k & 0x80) {
                kput(27);
                kput('[');
                k &= 0x7F;
            }
            kput(k);
        }
    }
}

int con_rx_ready(void)
{
    return kqh != kqt;
}

unsigned char con_getc(void)
{
    unsigned char c;

    while (kqh == kqt)
        ;
    c = kq[kqt];
    kqt = (kqt + 1) & 31;
    return c;
}
