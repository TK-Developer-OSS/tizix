/* tizix x86-ia16 : kernel console I/O -- tzcc version of ../../src/io.c (x86 branch).
 *   kputchar: "> file" redirection, pipe writer, /dev/null route, '\n' -> CR LF, then the console.
 *   kgetchar: "< file", pipe reader, the 1-byte push-back of con_break, then the console.
 *   kprintf : %s %d %u %% only. tzcc has no varargs: the arguments are read as fixed parameters
 *             (cdecl, the caller pops), so up to 4 values after fmt.
 */
#include "kw.h"
#include "fsx.h"

void con_putc(char c);
int con_rx_ready(void);
int con_getc(void);
char *tz_kwork(void);
void tz_irq_off(void);
void tz_irq_on(void);
void tz_kyield(void);
int redir_sink(int c);
int in_src(void);
int pipe_putc(int c);
int pipe_getc(void);
char pipe_is_reader(char blk);

#define ROUTE_DISCARD 1
#define ROUTE_PIPE 2

/* "> file" / "< file" belong to the slot that set them up (sh) and to the one child it starts meanwhile (the
 * redirected command runs in the foreground); other slots keep the console. Slot numbers are stored + 1, 0 = none.
 * A global switch let a background job's output into the file, and its f_write re-entered FatFs (and the BIOS)
 * under sh's own write. */
char redir_on;
char redir_kid;
char in_on;
char in_kid;
int con_ung;                    /* pushed-back byte + 1 (0 = empty; tzcc drops the -1 initializer) */

char cur_slot1(void)
{
    char *kw;
    kw = tz_kwork();
    return kw[KW_CUR_O] + 1;
}

void redir_enable(char on)
{
    redir_kid = 0;
    redir_on = 0;
    if (on)
        redir_on = cur_slot1();
}

void in_enable(char on)
{
    in_kid = 0;
    in_on = 0;
    if (on)
        in_on = cur_slot1();
}

/* kexec_file: slot n is being started by the current slot */
void redir_note_child(char n)
{
    char me;
    me = cur_slot1();
    if (redir_on && redir_on == me)
        redir_kid = n + 1;
    if (in_on && in_on == me)
        in_kid = n + 1;
}

int redir_mine(void)
{
    char me;
    if (redir_on == 0)
        return 0;
    me = cur_slot1();
    if (me == redir_on || me == redir_kid)
        return 1;
    return 0;
}

int in_mine(void)
{
    char me;
    if (in_on == 0)
        return 0;
    me = cur_slot1();
    if (me == in_on || me == in_kid)
        return 1;
    return 0;
}

int kputchar(int c)
{
    char *kw;
    unsigned r;
    if (redir_mine())
        return redir_sink(c);
    kw = tz_kwork();
    r = kw[KW_OUT_O + kw[KW_CUR_O]];
    if (r == ROUTE_PIPE)
        return pipe_putc(c);
    if (r == ROUTE_DISCARD)
        return c;
    if ((c & 0xFF) == 10)
        con_putc(13);
    con_putc(c);
    return c;
}

void putstr(char *s)
{
    while (*s) {
        kputchar(*s);
        s = s + 1;
    }
}

int con_pending(void)
{
    if (con_ung)
        return 1;
    return 0;
}

int kgetchar(void)
{
    char *kw;
    int c;
    if (in_mine())
        return in_src();
    kw = tz_kwork();
    if (pipe_is_reader(kw[KW_CUR_O]))
        return pipe_getc();
    for (;;) {
        tz_irq_off();
        if (con_ung) {
            c = con_ung - 1;
            con_ung = 0;
            tz_irq_on();
            return c;
        }
        if (con_rx_ready()) {
            c = con_getc() & 0xFF;
            tz_irq_on();
            return c;
        }
        tz_irq_on();
        tz_kyield();
    }
}

/* 1 if a Ctrl+C is waiting on the console; any other byte is kept for kgetchar */
int con_break(void)
{
    int c;
    tz_kyield();
    if (in_mine())
        return 0;
    tz_irq_off();
    if (con_ung) {
        tz_irq_on();
        return 0;
    }
    if (con_rx_ready() == 0) {
        tz_irq_on();
        return 0;
    }
    c = con_getc() & 0xFF;
    if ((c & 0x7F) == 3) {
        tz_irq_on();
        return 1;
    }
    con_ung = c + 1;
    tz_irq_on();
    return 0;
}

char pn_buf[6];
void putn(unsigned v)
{
    unsigned i;
    unsigned q;
    i = 0;
    for (;;) {
        q = (v >> 1) / 5;                       /* v / 10 with v >= 0x8000 allowed */
        pn_buf[i] = '0' + (v - q * 10);
        i = i + 1;
        v = q;
        if (v == 0)
            break;
    }
    while (i) {
        i = i - 1;
        kputchar(pn_buf[i]);
    }
}

void kp_arg(char f, unsigned v)
{
    int s;
    if (f == 's') {
        putstr(v);
        return;
    }
    if (f == 'd') {
        s = v;
        if (s < 0) {
            kputchar('-');
            v = 0 - v;
        }
        putn(v);
        return;
    }
    putn(v);
}

int kprintf(char *fmt, unsigned a0, unsigned a1, unsigned a2, unsigned a3)
{
    unsigned k;
    unsigned v;
    char c;
    k = 0;
    for (;;) {
        c = *fmt;
        if (c == 0)
            break;
        fmt = fmt + 1;
        if (c != '%') {
            kputchar(c);
            continue;
        }
        c = *fmt;
        fmt = fmt + 1;
        if (c == 's' || c == 'd' || c == 'u') {
            v = a0;
            if (k == 1) v = a1;
            if (k == 2) v = a2;
            if (k == 3) v = a3;
            k = k + 1;
            kp_arg(c, v);
        } else if (c == '%') {
            kputchar('%');
        } else {
            kputchar('%');
            kputchar(c);
        }
    }
    return 0;
}
