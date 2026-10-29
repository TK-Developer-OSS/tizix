/* tizix x86-ia16 : console (VGA text + 8042 keyboard + COM1) -- tzcc version of ../console.c.
 *
 * Same behavior as console.c, written in the C subset tzcc --march=ia16 compiles as-is:
 *   - port I/O, VGA memory and the interrupt flag go through the tz_* primitives in tz/glue.s
 *   - one declaration per line, no switch/ternary tricks with side effects, no '*p++' in arguments
 *   - values stay below 0x8000 where they are compared or divided (tzcc treats 16-bit values as signed)
 * Keyboard: IRQ1 is masked at the PIC and the 8042 is polled here (JIS layout, scan code set 1).
 * Output goes to VGA directly (no BIOS INT 10h, which does STI) and to COM1 when a 16550 is present.
 */
void tz_outb(unsigned port, unsigned val);
unsigned tz_inb(unsigned port);
void tz_io_delay(void);
void tz_vwrite(unsigned off, unsigned val);
void tz_vscroll(void);
unsigned tz_irq_save(void);
void tz_irq_restore(unsigned fl);
void tz_halt(void);
void tz_qemu_exit(void);

int serial_ok;
char kbd_buf[8];
int kbd_buf_len;
int kbd_buf_idx;

void con_init(void)
{
    /* detect a 16550: write test values to the scratch register (COM1+7) and read them back */
    serial_ok = 0;
    vattr = 7;
    tz_outb(0x3FF, 0x5A);
    tz_io_delay();
    if (tz_inb(0x3FF) == 0x5A) {
        tz_outb(0x3FF, 0xA5);
        tz_io_delay();
        if (tz_inb(0x3FF) == 0xA5)
            serial_ok = 1;
    }
    if (serial_ok) {
        tz_outb(0x3F9, 0x00);   /* interrupts off */
        tz_outb(0x3FB, 0x80);   /* DLAB=1 */
        tz_outb(0x3F8, 0x01);   /* divisor lo = 1 -> 115200bps */
        tz_outb(0x3F9, 0x00);   /* divisor hi = 0 */
        tz_outb(0x3FB, 0x03);   /* 8N1, DLAB=0 */
        tz_outb(0x3FA, 0xC7);   /* FIFO on, cleared, 14 byte threshold */
        tz_outb(0x3FC, 0x0B);   /* DTR|RTS|OUT2 */
    }
    /* mask IRQ1 (keyboard) at the PIC: the BIOS INT 09h must not take it, kb_poll reads it */
    tz_outb(0x21, tz_inb(0x21) | 0x02);
}

/* ---- VGA text 80x25 at B800:0000 ---- */
char vrow;
char vcol;
char vinit;
char vattr;                              /* set to 7 in con_init: tzcc drops initializers of scalar globals */
char sv_row;
char sv_col;
char esc_st;
char esc_n;
char esc_have;
unsigned esc_p[4];

void vcursor(void)
{
    unsigned pos;
    pos = vrow * 80 + vcol;
    tz_outb(0x3D4, 0x0F);
    tz_outb(0x3D5, pos & 0xFF);
    tz_outb(0x3D4, 0x0E);
    tz_outb(0x3D5, pos >> 8);
}

void v_clear_range(unsigned from, unsigned to)     /* cells [from, to) */
{
    unsigned i;
    for (i = from; i < to; i++)
        tz_vwrite(i * 2, 0x0720);
}

void vscroll(void)
{
    tz_vscroll();
    v_clear_range(1920, 2000);
}

/* ANSI subset: ESC[2J ESC[H ESC[r;cH ESC[K ESC[nm, arrows ESC[A..D, save/restore ESC[s ESC[u; others are dropped */
void v_csi(unsigned fc)
{
    unsigned n0;
    unsigned cur;
    unsigned np;
    unsigned i;
    unsigned r;
    unsigned c;
    unsigned d;
    unsigned ls;
    unsigned v;

    n0 = esc_p[0];
    cur = vrow * 80 + vcol;
    np = esc_n;
    if (esc_have || esc_n)
        np = np + 1;

    if (fc == 'H' || fc == 'f') {
        r = esc_p[0];
        if (r == 0) r = 1;
        c = esc_p[1];
        if (c == 0) c = 1;
        if (r > 25) vrow = 24;
        else vrow = r - 1;
        if (c > 80) vcol = 79;
        else vcol = c - 1;
    } else if (fc == 'J') {
        if (n0 == 2) v_clear_range(0, 2000);
        else if (n0 == 1) v_clear_range(0, cur + 1);
        else v_clear_range(cur, 2000);
    } else if (fc == 'K') {
        ls = vrow * 80;
        if (n0 == 2) v_clear_range(ls, ls + 80);
        else if (n0 == 1) v_clear_range(ls, cur + 1);
        else v_clear_range(cur, ls + 80);
    } else if (fc == 'A' || fc == 'B' || fc == 'C' || fc == 'D') {
        d = n0;
        if (d == 0) d = 1;
        if (fc == 'A') {
            if (vrow > d) vrow = vrow - d;
            else vrow = 0;
        } else if (fc == 'B') {
            if (vrow + d >= 25) vrow = 24;
            else vrow = vrow + d;
        } else if (fc == 'C') {
            if (vcol + d >= 80) vcol = 79;
            else vcol = vcol + d;
        } else {
            if (vcol > d) vcol = vcol - d;
            else vcol = 0;
        }
    } else if (fc == 's') {
        sv_row = vrow;
        sv_col = vcol;
    } else if (fc == 'u') {
        vrow = sv_row;
        vcol = sv_col;
    } else if (fc == 'm') {
        if (np == 0) vattr = 7;
        for (i = 0; i < np && i < 4; i++) {
            v = esc_p[i];
            if (v == 0 || v == 27 || v == 22) vattr = 7;
            else if (v == 1) vattr = vattr | 8;
            else if (v == 7) vattr = 0x70;
        }
    }
}

void vga_putc(unsigned c)
{
    unsigned i;

    if (!vinit) {                       /* first call: clear the screen, start at the top left */
        v_clear_range(0, 2000);
        vrow = 0;
        vcol = 0;
        vinit = 1;
    }
    if (esc_st == 1) {                  /* after ESC */
        if (c == '[') {
            esc_st = 2;
            esc_n = 0;
            esc_have = 0;
            esc_p[0] = 0;
            esc_p[1] = 0;
            esc_p[2] = 0;
            esc_p[3] = 0;
            return;
        }
        esc_st = 0;                     /* a lone ESC is dropped; this character is handled normally */
    } else if (esc_st == 2) {           /* CSI parameters / final character */
        if (c >= '0' && c <= '9') {
            esc_p[esc_n] = esc_p[esc_n] * 10 + (c - '0');
            esc_have = 1;
            return;
        }
        if (c == ';') {
            if (esc_n < 3) esc_n = esc_n + 1;
            esc_have = 0;
            return;
        }
        if (c == '?') return;           /* ESC[?25l etc. are ignored */
        esc_st = 0;
        v_csi(c);
        vcursor();
        return;
    }
    if (c == 27) {
        esc_st = 1;
        return;
    }
    if (c == '\r') {
        vcol = 0;
    } else if (c == '\n') {
        vcol = 0;                       /* LF is CR+LF on screen (#184) */
        vrow = vrow + 1;
    } else if (c == 8) {
        if (vcol) vcol = vcol - 1;
    } else if (c == '\t') {
        vcol = (vcol + 8) & ~7;
    } else if (c >= 0x20) {
        i = vattr;
        tz_vwrite((vrow * 80 + vcol) * 2, (i << 8) | c);
        vcol = vcol + 1;
    }
    if (vcol >= 80) {
        vcol = 0;
        vrow = vrow + 1;
    }
    if (vrow >= 25) {
        vscroll();
        vrow = 24;
    }
    vcursor();
}

void con_putc(char c)       /* gcc callers push a char without extending it: take only the low byte */
{
    unsigned fl;

    /* vrow/vcol/VRAM are updated with interrupts off (the kernel at IF=1 and a child's syscall at IF=0 may print at once) */
    fl = tz_irq_save();
    vga_putc(c);
    tz_irq_restore(fl);

    if (serial_ok) {
        for (;;) {
            if (tz_inb(0x3FD) & 0x20) break;     /* LSR THRE */
        }
        tz_outb(0x3F8, c);
    }
}

/* ---- keyboard: 8042 polled directly (ports 0x60/0x64), scan code set 1, JIS layout ---- */
unsigned char kb_norm[0x80] = {
    0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2D, 0x5E, 0x08, 0x09,
    0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x40, 0x5B, 0x0D, 0x00, 0x61, 0x73,
    0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x3B, 0x3A, 0x00, 0x00, 0x5D, 0x7A, 0x78, 0x63, 0x76,
    0x62, 0x6E, 0x6D, 0x2C, 0x2E, 0x2F, 0x00, 0x2A, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00,
};
unsigned char kb_shift[0x80] = {
    0x00, 0x1B, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x00, 0x3D, 0x7E, 0x08, 0x09,
    0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0x60, 0x7B, 0x0D, 0x00, 0x41, 0x53,
    0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x2B, 0x2A, 0x00, 0x00, 0x7D, 0x5A, 0x58, 0x43, 0x56,
    0x42, 0x4E, 0x4D, 0x3C, 0x3E, 0x3F, 0x00, 0x2A, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x00, 0x00,
};

unsigned kb_pend;                       /* 0 = none, else (ah<<8)|al; al==0 is a special key */
char kb_shift_on;
char kb_ctrl_on;
char kb_ext;

void kb_poll(void)
{
    unsigned sc;
    unsigned c;

    for (;;) {
        if (kb_pend) break;
        if (!(tz_inb(0x64) & 0x01)) break;
        sc = tz_inb(0x60);
        if (sc == 0xE0) {
            kb_ext = 1;
            continue;
        }
        if (sc & 0x80) {                          /* break code (key released) */
            sc = sc & 0x7F;
            if (sc == 0x2A || sc == 0x36) kb_shift_on = 0;
            else if (sc == 0x1D) kb_ctrl_on = 0;
            kb_ext = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) {
            kb_shift_on = 1;
            continue;
        }
        if (sc == 0x1D) {
            kb_ctrl_on = 1;
            continue;
        }
        if (kb_ext) {                             /* arrows: special key (al=0, ah=scan code) */
            kb_ext = 0;
            if (sc == 0x48 || sc == 0x50 || sc == 0x4B || sc == 0x4D)
                kb_pend = sc << 8;
            else if (sc == 0x53)                  /* Delete -> 0x7F */
                kb_pend = 0x7F;
            continue;
        }
        if (sc >= 0x7E) continue;
        if (kb_shift_on) c = kb_shift[sc];
        else c = kb_norm[sc];
        if (!c) continue;
        if (kb_ctrl_on) {
            if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
            else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;
            else if (c == ']') c = 0x1D;
            else if (c == '[') c = 0x1B;
        }
        kb_pend = c;
    }
}

int con_rx_ready(void)
{
    if (kbd_buf_idx < kbd_buf_len)
        return 1;
    if (serial_ok && (tz_inb(0x3FD) & 0x01))
        return 1;
    kb_poll();
    return kb_pend != 0;
}

unsigned con_getc(void)
{
    unsigned c;
    unsigned k;
    unsigned ah;
    unsigned code;

    /* 1. return what is left in the escape-sequence buffer */
    if (kbd_buf_idx < kbd_buf_len) {
        c = kbd_buf[kbd_buf_idx];
        kbd_buf_idx = kbd_buf_idx + 1;
        return c;
    }
    kbd_buf_len = 0;
    kbd_buf_idx = 0;

    /* 2. poll COM1 and the keyboard */
    for (;;) {
        if (serial_ok && (tz_inb(0x3FD) & 0x01)) {
            c = tz_inb(0x3F8);
            if (c == 0x1D) {                      /* QEMU escape hatch: Ctrl-] */
                tz_qemu_exit();
                for (;;)
                    tz_halt();
            }
            return c;
        }
        kb_poll();
        if (kb_pend) {
            k = kb_pend;
            kb_pend = 0;
            c = k & 0xFF;
            if (c != 0)
                return c;
            ah = k >> 8;                          /* special key -> ESC [ A/B/C/D */
            code = 0;
            if (ah == 0x48) code = 'A';
            else if (ah == 0x50) code = 'B';
            else if (ah == 0x4D) code = 'C';
            else if (ah == 0x4B) code = 'D';
            if (code != 0) {
                kbd_buf[0] = 0x1B;
                kbd_buf[1] = '[';
                kbd_buf[2] = code;
                kbd_buf_len = 3;
                kbd_buf_idx = 1;
                return 0x1B;
            }
        }
    }
}

void con_puts(char *s)
{
    unsigned c;
    for (;;) {
        c = *s;
        if (c == 0) break;
        if (c == '\n') con_putc('\r');
        con_putc(c);
        s = s + 1;
    }
}
