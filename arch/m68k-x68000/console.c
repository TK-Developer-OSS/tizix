/* arch/m68k-x68000 console.c - console through the X68000 IOCS (TRAP #15).
 *   Output is _B_PUTC ($20); the IOCS console interprets ESC sequences.
 *   Input is _B_KEYSNS ($01) / _B_KEYINP ($00) and never waits (the caller's KYIELD
 *   loop waits).  Arrow keys become the ANSI sequences ESC [ A..D. */
#include "plat.h"

extern long iocs_call(long no, long d1, long d2, long d3, void *a1);

#define IOCS_B_KEYINP 0x00
#define IOCS_B_KEYSNS 0x01
#define IOCS_B_PUTC   0x20

static unsigned char pend[4];
static unsigned char pend_n, pend_i;

/* Every byte printed is also kept in a ring at CONLOG_ADDR.  The regression test
 * (tools/xm6run.py) reads it from the XM6G process memory.  First 8 bytes = "TZXLOG01",
 * next 4 = total bytes written. */
#define CONLOG_SIZE 4080
struct conlog {
    char          magic[8];
    unsigned long total;
    unsigned char buf[CONLOG_SIZE];
};
#define CONLOG ((volatile struct conlog *)CONLOG_ADDR)

void con_log_init(void)
{
    static const char m[8] = "TZXLOG01";
    unsigned i;

    CONLOG->total = 0;
    for (i = 0; i < 8; i++)
        CONLOG->magic[i] = m[i];
}

static unsigned char esc_st;    /* 0 = text, 1 = after ESC, 2 = after ESC [ */

void con_putc(char c)
{
    CONLOG->buf[CONLOG->total % CONLOG_SIZE] = (unsigned char)c;
    CONLOG->total++;
    /* The IOCS console ignores a bare ESC[H (no parameters); send ESC[1;1H instead */
    if (esc_st == 2 && c == 'H') {
        iocs_call(IOCS_B_PUTC, '1', 0, 0, 0);
        iocs_call(IOCS_B_PUTC, ';', 0, 0, 0);
        iocs_call(IOCS_B_PUTC, '1', 0, 0, 0);
    }
    esc_st = (c == 0x1B) ? 1 : (esc_st == 1 && c == '[') ? 2 : 0;
    iocs_call(IOCS_B_PUTC, (unsigned char)c, 0, 0, 0);
}

/* Prepare the next byte to return: take one key and queue its bytes in pend[] */
static void fetch(void)
{
    long k;

    while (pend_i >= pend_n) {
        k = iocs_call(IOCS_B_KEYSNS, 0, 0, 0, 0);
        if (k == 0)
            return;
        k = iocs_call(IOCS_B_KEYINP, 0, 0, 0, 0);
        pend_i = pend_n = 0;
        if (k & 0xFF) {
            pend[pend_n++] = (unsigned char)k;
        } else {
            unsigned char sc = (unsigned char)(k >> 8);
            unsigned char fin = 0;
            switch (sc) {
            case 0x3C: fin = 'A'; break;     /* ↑ */
            case 0x3E: fin = 'B'; break;     /* ↓ */
            case 0x3D: fin = 'C'; break;     /* → */
            case 0x3B: fin = 'D'; break;     /* ← */
            }
            if (fin) {
                pend[pend_n++] = 0x1B;
                pend[pend_n++] = '[';
                pend[pend_n++] = fin;
            }
        }
    }
}

int con_rx_ready(void)
{
    fetch();
    return pend_i < pend_n;
}

unsigned char con_getc(void)
{
    while (!con_rx_ready()) {}
    return pend[pend_i++];
}
