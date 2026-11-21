#include "console.h"

/* ------------------------------------------------------------------
 * Layer 1 for the TRS-80 Model 4P (memory map 2):
 *   Screen: 80x24 text at 0xF800 (one byte a cell, ASCII 0x20-0x7F; the graphics characters are not used).
 *     Scrolling is a block move in assembly; ESC [ H / J / K, CR, LF and BS are understood.
 *   Keyboard: memory mapped at 0xF400 + (1 << row); each of the 8 rows returns 8 keys, pressed = 1.  Scanned from the
 *     30Hz timer ISR (kb_isr), edge detected, no key repeat.
 *   Serial: the UART (ports 0xE8-0xEB, 19200 8N1 set in crt0.s) is a second console: every character goes to it as well,
 *     and bytes that arrive are read as typed keys (no CR/LF translation).  This is what the xtrs test harness uses (xtrs -serial PTY).
 *   Keys the TRS-80 does not have are on Clear + a key (ClrQ | W \ E [ R ] T { Y } U ~ I ` O _ P ^).
 *     Break = Ctrl+C, Left = backspace (0x08), Shift+Left/Right/Up/Down = cursor keys (ESC [ D C A B), Ctrl+letter = control.
 * ------------------------------------------------------------------ */
__sfr __at 0xEA UARTST;
__sfr __at 0xEB UARTD;

#define VRAM    ((volatile unsigned char *)0xF800)
#define KBD(r)  (*(volatile unsigned char *)(0xF400 + (1 << (r))))
#define COLS    80
#define ROWS    24

static unsigned char ccol, crow, in_esc, par[2], npar;

static void vfill(unsigned char *p, unsigned n)
{
    while (n--) *p++ = ' ';
}

static void scroll(void) __naked
{
    __asm
        ld      hl, #0xF800 + 80
        ld      de, #0xF800
        ld      bc, #80 * 23
        ldir
        ld      hl, #0xF800 + 80 * 23
        ld      de, #0xF800 + 80 * 23 + 1
        ld      bc, #79
        ld      (hl), #0x20
        ldir
        ret
    __endasm;
}

static void newline(void)
{
    if (++crow >= ROWS) {
        crow = ROWS - 1;
        scroll();
    }
}

static unsigned char inited;

static void screen_putc(unsigned char c)
{
    if (!inited) {
        inited = 1;
        vfill((unsigned char *)VRAM, COLS * ROWS);
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
                vfill((unsigned char *)VRAM, COLS * ROWS);
                ccol = crow = 0;
            } else if (c == 'K')
                vfill((unsigned char *)VRAM + crow * COLS + ccol, COLS - ccol);
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
        VRAM[crow * COLS + ccol] = c;
        if (++ccol >= COLS) { ccol = 0; newline(); }
    }
}

void con_putc(char c)
{
    unsigned char n;

    screen_putc((unsigned char)c);
    if (UARTST == 0xFF) return;            /* no UART (xtrs -serial "" reads 0xFF) */
    for (n = 0; n < 250 && !(UARTST & 0x40); n++)
        ;
    UARTD = (unsigned char)c;
}

/* ------------------------------------------------------------------
 * keys: row r bit b -> kt[r * 8 + b]
 *   row 0: @ A-G   row 1: H-O   row 2: P-W   row 3: X Y Z   row 4: 0-7   row 5: 8 9 : ; , - . /
 *   row 6: Enter Clear Break Up Down Left Right Space      row 7: Shift Shift Ctrl Caps F1 F2 F3 (-)
 * ------------------------------------------------------------------ */
static const char kt[64] =
    "@abcdefg" "hijklmno" "pqrstuvw" "xyz\0\0\0\0\0" "01234567" "89:;,-./"
    "\r\0\x03\0\0\x08\0 " "\0\0\0\0\0\0\0\0";
static const char ks[64] =
    "\0ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ\0\0\0\0\0" "0!\"#$%&'" "()*+<=>?"
    "\r\0\x03\0\0\0\0 " "\0\0\0\0\0\0\0\0";
/* Clear + letter: row 1-2 letters */
static const char kc[] = "wqertyuiop";            /* Clear + key kc[i] -> kcv[i] */
static const char kcv[] = "\\|[]{}~`_^";

static unsigned char kprev[8];
static unsigned char kq[32];
static volatile unsigned char kqh, kqt;

static void kput(unsigned char k)
{
    if (k && ((kqh + 1) & 31) != kqt) { kq[kqh] = k; kqh = (kqh + 1) & 31; }
}

void kb_isr(void)
{
    unsigned char r, i, m, k, sh, ctl, clr, b, j;

    b = KBD(7);
    sh = b & 3;
    ctl = b & 4;
    clr = KBD(6) & 2;
    for (r = 0; r < 7; r++) {
        b = KBD(r);
        m = (unsigned char)(b & ~kprev[r]);       /* released -> pressed */
        kprev[r] = b;
        for (i = 0; m; i++, m >>= 1) {
            if (!(m & 1)) continue;
            k = sh ? ks[r * 8 + i] : kt[r * 8 + i];
            if (r == 6 && i >= 3 && i <= 6) {     /* Up Down Left Right */
                if (i == 5 && !sh) { kput(8); continue; }
                kput(27);
                kput('[');
                k = "ABDC"[i - 3];
            } else if (clr && k) {
                for (j = 0; kc[j]; j++)
                    if (kc[j] == kt[r * 8 + i]) { k = kcv[j]; break; }
            } else if (ctl && k >= 'a' && k <= 'z') k &= 0x1F;
            kput(k);
        }
    }
}

/* The UART holds one byte, and the main code is the only reader of it (the key queue is written from the ISR only), so
 * the two sources need no locking: a UART byte, when there is one, is returned first. */
static unsigned char uart_rx(void)
{
    unsigned char s = UARTST;
    return s != 0xFF && (s & 0x80);
}

int con_rx_ready(void)
{
    return uart_rx() || kqh != kqt;
}

unsigned char con_getc(void)
{
    unsigned char c;

    for (;;) {
        if (uart_rx()) return UARTD;
        if (kqh != kqt) break;
    }
    c = kq[kqt];
    kqt = (kqt + 1) & 31;
    return c;
}
