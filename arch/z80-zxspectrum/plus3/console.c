#include "console.h"

/* ------------------------------------------------------------------
 * Layer 1 for the ZX Spectrum +3 (all-RAM configuration 0):
 *   Screen: 32x24 text on the 256x192 bitmap, white on black, 8x8 cells with a 5x7 font.  The bitmap is bank 5, which is
 *     only visible in configuration 2, so the drawing is done by hi.s (it lives in bank 3 at 0xF000, interrupts off, private
 *     stack); this file only keeps the cursor and the ESC [ H / J / K, CR, LF, BS parsing and calls into hi.s.
 *     hi.s also keeps a text copy of the screen (the shadow, 32x24 bytes at 0xF400) which the test reads.
 *   Keyboard: the 8 half-rows (port 0xFE with the row in the high byte), scanned from the 50Hz frame ISR (kb_isr),
 *     edge detected, no key repeat.  Symbol Shift gives the printed symbols (as on the keycaps), Caps Shift + 5 6 7 8 are
 *     the cursor keys (ESC [ D B A C), Caps Shift + 0 = backspace, Caps Shift + Space = Break (^C), both shifts + a
 *     letter = control character.
 * ------------------------------------------------------------------ */
#define HI_C    (*(volatile unsigned char *)0xF3F0)
#define HI_ROW  (*(volatile unsigned char *)0xF3F1)
#define HI_COL  (*(volatile unsigned char *)0xF3F2)
#define MOTOR   (*(volatile unsigned char *)0xF3F6)
#define COLS    32
#define ROWS    24

static void hi_draw(void)    __naked { __asm jp 0xF000 __endasm; }
static void hi_clrrow(void)  __naked { __asm jp 0xF003 __endasm; }
static void hi_moverow(void) __naked { __asm jp 0xF006 __endasm; }
static void hi_cursor(void)  __naked { __asm jp 0xF009 __endasm; }
static void hi_init(void)    __naked { __asm jp 0xF00C __endasm; }

static unsigned char ccol, crow, in_esc, par[2], npar, inited, curon;

static void cell(unsigned char c, unsigned char row, unsigned char col)
{
    HI_C = c;
    HI_ROW = row;
    HI_COL = col;
    hi_draw();
}

static void newline(void)
{
    unsigned char r;

    if (++crow >= ROWS) {
        crow = ROWS - 1;
        for (r = 1; r < ROWS; r++) {
            HI_ROW = r;
            hi_moverow();
        }
        HI_ROW = ROWS - 1;
        hi_clrrow();
    }
}

static void screen_putc(unsigned char c)
{
    unsigned char i;

    if (!inited) {
        inited = 1;
        MOTOR = 0x08;                        /* crt0.s left the disk motor on; hi.s keeps that bit in its 1FFD writes */
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
    screen_putc((unsigned char)c);
}

/* ------------------------------------------------------------------
 * keys: row r (port selected by the high byte 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F), bit b (0 = outermost) -> [r * 5 + b]
 *   row 0: CS Z X C V   row 1: A S D F G   row 2: Q W E R T   row 3: 1 2 3 4 5
 *   row 4: 0 9 8 7 6    row 5: P O I U Y   row 6: Enter L K J H   row 7: Space SS M N B
 * ------------------------------------------------------------------ */
static const char kt[41] =
    "\0zxcv" "asdfg" "qwert" "12345" "09876" "poiuy" "\rlkjh" " \0mnb";
static const char ks[41] =
    "\0:`?/" "~|\\{}" "\0\0\0<>" "!@#$%" "_)('&" "\";\0][" "\r=+-^" " \0.,*";

static volatile unsigned char kbsel, kbval;
static unsigned char kprev[8];
static unsigned char kq[32];
static volatile unsigned char kqh, kqt;

static void kb_read(void) __naked
{
    __asm
        ld      a, (_kbsel)
        ld      b, a
        ld      c, #0xFE
        in      a, (c)
        ld      (_kbval), a
        ret
    __endasm;
}

static void kput(unsigned char k)
{
    if (k && ((kqh + 1) & 31) != kqt) { kq[kqh] = k; kqh = (kqh + 1) & 31; }
}

static unsigned char kball;
static unsigned char kb_any(void);
static void kb_scan(void)
{
    unsigned char r, b, m, i, k, cs, ss, now[8];

    kball = kb_any();

    for (r = 0; r < 8; r++) {
        kbsel = (unsigned char)~(1 << r);
        kb_read();
        now[r] = (unsigned char)(~kbval & 0x1F);
    }
    cs = now[0] & 1;
    ss = now[7] & 2;
    for (r = 0; r < 8; r++) {
        m = (unsigned char)(now[r] & ~kprev[r]);
        kprev[r] = now[r];
        for (i = 0; m; i++, m >>= 1) {
            if (!(m & 1)) continue;
            b = r * 5 + i;
            k = (ss && !cs) ? ks[b] : kt[b];
            if (cs && !ss) {
                if (r == 3 && i == 4) { kput(27); kput('['); k = 'D'; }          /* Caps + 5: left */
                else if (r == 4 && i == 0) k = 8;                                 /* Caps + 0: delete */
                else if (r == 4 && i == 2) { kput(27); kput('['); k = 'C'; }      /* Caps + 8: right */
                else if (r == 4 && i == 3) { kput(27); kput('['); k = 'A'; }      /* Caps + 7: up */
                else if (r == 4 && i == 4) { kput(27); kput('['); k = 'B'; }      /* Caps + 6: down */
                else if (r == 7 && i == 0) k = 3;                                 /* Caps + Space: Break */
                else if (k >= 'a' && k <= 'z') k -= 32;
            } else if (cs && ss) {
                if (kt[b] >= 'a' && kt[b] <= 'z') k = kt[b] & 0x1F;               /* both shifts + letter: control */
                else k = 0;
            }
            kput(k);
        }
    }
}

void kb_isr(void)
{
    kb_scan();
}

/* The frame interrupt is lost whenever it arrives while drawing or disk code runs with DI, so the idle loops scan too. */
static unsigned char kb_any(void) __naked
{
    __asm
        ld      bc, #0x00FE
        in      a, (c)
        and     a, #0x1F
        ld      l, a
        ret
    __endasm;
}

/* Called from the idle loops (kgetchar's wait loop calls con_rx_ready with DI).  The ZX frame interrupt is a ~32 T-state
 * pulse, so it is lost whenever it falls inside a DI section, and that loop is mostly DI: the keys are polled here too.
 * A single IN of all rows at once, and a full scan only when something changed.  The scan always runs with DI (the ISR
 * must not scan at the same time); EI follows only if LD A,I said the interrupts were on (a misread "off" leaves them
 * off until the caller's own EI, which is harmless). */
static void kb_poll(void) __naked
{
    __asm
        call    _kb_any
        ld      a, (_kball)
        cp      a, l
        ret     z
        ld      a, i
        di
        push    af
        call    _kb_scan
        pop     af
        ret     po
        ei
        ret
    __endasm;
}

static void show_cursor(void)
{
    if (!curon && inited) {
        curon = 1;
        HI_ROW = crow;
        HI_COL = ccol;
        hi_cursor();
    }
}

/* Wait for the next frame interrupt with the interrupts on, then restore the caller's state.  Only the idle paths get
 * here (kgetchar's and con_break's wait loops, which call con_rx_ready under DI and loop while it says 0): on the ZX the
 * interrupt is a short pulse that those mostly-DI loops would miss, and the clock and the preemption would stall.
 * Saying "no key" after the wait keeps their ordering rules (nothing is taken from the queue here). */
static void frame_wait(void) __naked
{
    __asm
        ld      a, i
        push    af
        ei
        halt
        pop     af
        ret     pe
        di
        ret
    __endasm;
}

int con_rx_ready(void)
{
    if (kqh == kqt) kb_poll();
    if (kqh != kqt) return 1;
    show_cursor();
    frame_wait();
    return 0;
}

unsigned char con_getc(void)
{
    unsigned char c;

    while (kqh == kqt) {
        show_cursor();
        kb_poll();
    }
    c = kq[kqt];
    kqt = (kqt + 1) & 31;
    return c;
}
