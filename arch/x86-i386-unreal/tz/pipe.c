/* tizix x86-ia16 : kernel pipe (one at a time, 4KB linear buffer, no wrap) -- tzcc version of ../../src/pipe.c.
 *   writer putchar -> ROUTE_PIPE -> pipe_putc, reader getchar -> pipe_is_reader -> pipe_getc.
 *   More than 4096 bytes in total sets ovf; krun_pipe then reports it (sh prints "out of memory").
 */
#include "kw.h"

char *tz_kwork(void);
void tz_irq_off(void);
void tz_irq_on(void);
void proc_block(void);
void proc_wake(char blk);
char kexec_argv(char *fname, char *argpack, char argc);
int con_break(void);
int kputchar(int c);

#define BUFCAP 4096
#define ROUTE_CONSOLE 0
#define ROUTE_PIPE 2

char p_active;
char p_wblk;
char p_rblk;
char p_weof;
char p_rgone;
char p_ovf;
unsigned p_head;
unsigned p_tail;
char p_buf[BUFCAP];

int pipe_setup(char rblk)
{
    tz_irq_off();
    p_rblk = rblk;
    p_wblk = 0xFF;                      /* placeholder until the writer starts */
    p_weof = 0;
    p_rgone = 0;
    p_ovf = 0;
    p_head = 0;
    p_tail = 0;
    p_active = 1;
    tz_irq_on();
    return 1;
}

void pipe_teardown(void)
{
    tz_irq_off();
    p_active = 0;
    tz_irq_on();
}

/* the slot is free (or the placeholder 0xFF is still there: "not yet") */
int slot_gone(unsigned blk)
{
    char *kw;
    if (blk > PROC_BLOCK_MAX)
        return 0;
    kw = tz_kwork();
    if (kw[blk] == 0)
        return 1;
    return 0;
}

int reader_gone(void)
{
    if (p_rgone)
        return 1;
    return slot_gone(p_rblk);
}

int writer_gone(void)
{
    if (p_weof)
        return 1;
    return slot_gone(p_wblk);
}

int pipe_putc(int c)
{
    unsigned was;
    tz_irq_off();
    if (reader_gone()) {
        p_rgone = 1;
        tz_irq_on();
        return -1;
    }
    if (p_tail < BUFCAP) {
        was = p_tail - p_head;
        p_buf[p_tail] = c;
        p_tail = p_tail + 1;
        tz_irq_on();
        if (was == 0)
            proc_wake(p_rblk);          /* it was empty: wake the reader */
        return c;
    }
    p_ovf = 1;
    tz_irq_on();
    proc_wake(p_rblk);
    return -1;
}

int pipe_getc(void)
{
    int c;
    for (;;) {
        tz_irq_off();
        if (p_head < p_tail) {
            c = p_buf[p_head];
            p_head = p_head + 1;
            tz_irq_on();
            return c;
        }
        if (p_weof || p_ovf || writer_gone()) {
            tz_irq_on();
            return -1;
        }
        tz_irq_on();
        proc_wake(p_wblk);
        proc_block();
    }
}

/* tail after a pipe: wait for the writer, then print the last `want` lines of the buffer. 0 = not a pipe reader */
int pipe_tail(unsigned want)
{
    unsigned n;
    unsigned i;
    unsigned start;
    unsigned end;
    unsigned nl;
    for (;;) {
        tz_irq_off();
        if (p_active == 0) {
            tz_irq_on();
            return 0;
        }
        if (p_weof || p_ovf || writer_gone())
            break;
        tz_irq_on();
        proc_wake(p_wblk);
        proc_block();
    }
    n = p_tail;
    tz_irq_on();
    if (n == 0)
        return 1;
    end = n;
    if (p_buf[end - 1] == 10)
        end = end - 1;
    start = 0;
    nl = 0;
    i = end;
    while (i > 0) {
        i = i - 1;
        if (p_buf[i] != 10)
            continue;
        nl = nl + 1;
        if (nl >= want) {
            start = i + 1;
            break;
        }
    }
    for (i = start; i < n; i++)
        kputchar(p_buf[i]);
    return 1;
}

char pipe_is_reader(char blk)
{
    if (p_active && p_rblk == blk)
        return 1;
    return 0;
}

void pipe_note_exit(char blk)
{
    if (p_active == 0)
        return;
    if (p_wblk == blk && p_weof == 0) {
        p_weof = 1;
        proc_wake(p_rblk);
    } else if (p_rblk == blk && p_rgone == 0) {
        p_rgone = 1;
        proc_wake(p_wblk);
    }
}

/* "/bin/<name>.bin" (a name starting with '/' only gets ".bin") */
char pf_name[24];
void pipe_fname(char *name)
{
    unsigned i;
    unsigned j;
    i = 0;
    j = 0;
    if (name[0] != '/') {
        pf_name[0] = '/';
        pf_name[1] = 'b';
        pf_name[2] = 'i';
        pf_name[3] = 'n';
        pf_name[4] = '/';
        j = 5;
    }
    while (name[i] && j < 18) {
        pf_name[j] = name[i];
        i = i + 1;
        j = j + 1;
    }
    pf_name[j] = '.';
    pf_name[j + 1] = 'b';
    pf_name[j + 2] = 'i';
    pf_name[j + 3] = 'n';
    pf_name[j + 4] = 0;
}

void kill_slot(unsigned blk)
{
    char *kw;
    if (blk == 0 || blk > PROC_BLOCK_MAX)
        return;
    kw = tz_kwork();
    kw[blk] = 0;
}

/* A | B: start both, connect, wait for both. 0 = ok / 1 = cannot start / 2 = more than 4KB / 3 = Ctrl+C */
char krun_pipe(char *ln, char *lp, char lc, char *rn, char *rp, char rc)
{
    char *kw;
    unsigned wb;
    unsigned rb;
    unsigned ret;
    kw = tz_kwork();
    ret = 0;
    pipe_fname(rn);
    rb = kexec_argv(pf_name, rp, rc);
    if (rb == 0 || rb == 0xFF)
        return 1;
    pipe_setup(rb);
    pipe_fname(ln);
    wb = kexec_argv(pf_name, lp, lc);
    if (wb == 0 || wb == 0xFF) {
        pipe_teardown();
        kill_slot(rb);
        return 1;
    }
    p_wblk = wb;
    kw[KW_OUT_O + wb] = ROUTE_PIPE;
    while (kw[wb] != 0 || kw[rb] != 0) {
        if (kw[wb] == 0)
            pipe_note_exit(wb);
        if (kw[rb] == 0) {
            pipe_note_exit(rb);
            if (kw[wb] != 0)
                kill_slot(wb);
        }
        if (con_break()) {
            kill_slot(wb);
            kill_slot(rb);
            ret = 3;
        }
    }
    kw[KW_OUT_O + wb] = ROUTE_CONSOLE;
    if (ret == 0 && p_active && p_ovf)
        ret = 2;
    pipe_teardown();
    return ret;
}
