#include "console.h"

/* ------------------------------------------------------------------
 * Layer 1 for the Amstrad CPC 6128 (RAM configuration 1):
 *   Screen: 80x25 text in mode 2 (640x200, one bit a pixel), white on black, 8x8 cells.  The screen is bank 3, which the
 *     CPU only sees in configuration 3, so the drawing is done by hi.s (in bank 7 at 0xF000, interrupts off, private
 *     stack); this file only keeps the cursor and the ESC [ H / J / K, CR, LF, BS parsing and calls into hi.s.
 *     hi.s also keeps a text copy of the screen (the shadow, 80x25 bytes at 0xF400) which the test reads.
 *   Keyboard: the 10 lines of the matrix (through the PPI and the sound chip's port A), scanned at 50Hz from the ISR
 *     (kb_isr), edge detected, no key repeat.  Shift gives the symbols printed on the keys, Control + a letter is a
 *     control character (Control + C = Break), Caps Lock toggles capitals, the cursor keys send ESC [ A-D, DEL = backspace.
 *     The tables are in hi.s at 0xFF00 (keys.inc).
 * ------------------------------------------------------------------ */
#define HI_C    (*(volatile unsigned char *)0xF3F0)
#define HI_ROW  (*(volatile unsigned char *)0xF3F1)
#define HI_COL  (*(volatile unsigned char *)0xF3F2)
#define KEYS    ((const unsigned char *)0xFF00)
#define KEYS_S  ((const unsigned char *)0xFF50)
#define COLS    80
#define ROWS    25

static void hi_draw(void)    __naked { __asm jp 0xF000 __endasm; }
static void hi_clrrow(void)  __naked { __asm jp 0xF003 __endasm; }
static void hi_scroll(void)  __naked { __asm jp 0xF006 __endasm; }
static void hi_cursor(void)  __naked { __asm jp 0xF009 __endasm; }
static void hi_init(void)    __naked { __asm jp 0xF00C __endasm; }

static unsigned char ccol, crow, in_esc, par[2], npar, inited, curon;
/* set while con_putc runs.  show_cursor (always called with the interrupts off, from kgetchar's and con_break's loops)
 * must not draw then: a process preempted in the middle of con_putc would later erase the cursor at its new column and
 * leave the one drawn at the old column on the screen. */
static volatile unsigned char busy;

static void cell(unsigned char c, unsigned char row, unsigned char col)
{
    HI_C = c;
    HI_ROW = row;
    HI_COL = col;
    hi_draw();
}

static void newline(void)
{
    if (++crow >= ROWS) {
        crow = ROWS - 1;
        hi_scroll();
    }
}

static void screen_putc(unsigned char c)
{
    unsigned char i;

    if (!inited) {
        inited = 1;
        hi_init();
    }
    if (curon) {
        curon = 0;
        HI_ROW = crow;
        HI_COL = ccol;
        hi_cursor();
    }
    if (in_esc == 2) {                       /* ESC [ params final */
        if (c >= '0' && c <= '9') par[npar] = par[npar] * 10 + (c - '0');
        else if (c == ';') npar = 1;
        else {
            in_esc = 0;
            if (c == 'H') {
                crow = par[0] ? par[0] - 1 : 0;
                ccol = par[1] ? par[1] - 1 : 0;
                if (crow >= ROWS) crow = ROWS - 1;
                if (ccol >= COLS) ccol = COLS - 1;
            } else if (c == 'J') {
                hi_init();
                ccol = crow = 0;
            } else if (c == 'K')
                for (i = ccol; i < COLS; i++) cell(' ', crow, i);
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
    else if (c >= 32 && c < 127) {
        cell(c, crow, ccol);
        if (++ccol >= COLS) { ccol = 0; newline(); }
    }
}

void con_putc(char c)
{
    busy = 1;
    screen_putc((unsigned char)c);
    busy = 0;
}

/* ------------------------------------------------------------------ keyboard */
unsigned char isrdiv;                        /* crt0.s: counts the 300Hz interrupts down to the 50Hz ones */
static unsigned char kbnow[10], kprev[10], caps;
static unsigned char kq[32];
static volatile unsigned char kqh, kqt;

/* the 10 matrix lines into kbnow[] (1 = pressed): PSG register 14 is the keyboard, PPI port C picks the line */
static void kb_read(void) __naked
{
    __asm
        ld      hl, #_kbnow
        ld      bc, #0xF40E             ; PPI port A = 14 (the PSG register)
        out     (c), c
        ld      bc, #0xF6C0             ; PSG: latch the register number
        out     (c), c
        ld      bc, #0xF600             ; PSG inactive
        out     (c), c
        ld      bc, #0xF792             ; PPI port A input
        out     (c), c
        ld      e, #0x40                ; PSG read, line 0
1$:     ld      b, #0xF6
        out     (c), e
        ld      b, #0xF4
        in      a, (c)
        cpl
        ld      (hl), a
        inc     hl
        inc     e
        ld      a, e
        cp      a, #0x4A
        jr      nz, 1$
        ld      bc, #0xF782             ; PPI port A output again
        out     (c), c
        ld      bc, #0xF600
        out     (c), c
        ret
    __endasm;
}

static void kput(unsigned char k)
{
    if (((kqh + 1) & 31) != kqt) { kq[kqh] = k; kqh = (kqh + 1) & 31; }
}

void kb_isr(void)
{
    unsigned char r, i, m, k, b, sh, ct;

    kb_read();
    sh = kbnow[2] & 0x20;
    ct = kbnow[2] & 0x80;
    for (r = 0; r < 10; r++) {
        m = (unsigned char)(kbnow[r] & ~kprev[r]);
        kprev[r] = kbnow[r];
        for (i = 0; m; i++, m >>= 1) {
            if (!(m & 1)) continue;
            b = r * 8 + i;
            if (b == 8 * 8 + 6) { caps ^= 1; continue; }          /* Caps Lock */
            k = sh ? KEYS_S[b] : KEYS[b];
            if (!k) continue;
            if (k & 0x80) { kput(27); kput('['); k &= 0x7F; }    /* cursor keys */
            else if (ct) {
                k = KEYS[b];
                if (k < 'a' || k > 'z') continue;
                k &= 0x1F;                                      /* Control + letter */
            } else if (caps && k >= 'a' && k <= 'z') k -= 32;
            kput(k);
        }
    }
}

static void show_cursor(void)
{
    if (!curon && inited && !busy) {
        curon = 1;
        HI_ROW = crow;
        HI_COL = ccol;
        hi_cursor();
    }
}

int con_rx_ready(void)
{
    if (kqh != kqt) return 1;
    show_cursor();
    return 0;
}

unsigned char con_getc(void)
{
    unsigned char c;

    while (kqh == kqt) show_cursor();
    c = kq[kqt];
    kqt = (kqt + 1) & 31;
    return c;
}
