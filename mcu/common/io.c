/* io.c - tiny output helpers (no printf: it costs far too much flash/RAM) */
#include "mcu.h"

static char *sink;
static uint8_t sink_len, sink_max;

void msink_begin(char *buf, uint8_t max)
{
    sink = buf;
    sink_max = max;
    sink_len = 0;
    buf[0] = 0;
}

void msink_end(void)
{
    sink = 0;
}

void mputc(char c)
{
    if (sink) {
        if (sink_len < sink_max - 1) {
            sink[sink_len++] = c;
            sink[sink_len] = 0;
        }
        return;
    }
    if (c == '\n')
        hal_putc('\r');
    hal_putc(c);
}

void mputs(const char *s)
{
    while (*s)
        mputc(*s++);
}

void mputs_P(const char *s)
{
    char c;
    while ((c = (char)mgetb_P(s++)) != 0)
        mputc(c);
}

void mputu(uint32_t v)
{
    char t[10];
    uint8_t i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i)
        mputc(t[--i]);
}

int mparse_uint(const char *s, unsigned *v)
{
    unsigned r = 0;
    if (*s < '0' || *s > '9')
        return -1;
    while (*s >= '0' && *s <= '9') {
        if (r > 6553)
            return -1;
        r = r * 10 + (unsigned)(*s++ - '0');
    }
    if (*s)
        return -1;
    *v = r;
    return 0;
}
