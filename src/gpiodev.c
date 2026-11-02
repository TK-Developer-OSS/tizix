/* gpiodev.c - GPIO の文字列インターフェース(builtin の pico-gpio と /dev/gpio が共有)。
 *   文法は gpiodev.h。ハードは hook(pin_*)の先。esp32-gpio(arch/esp32-wroom-32e/cmd/gpio.c)
 *   と同じ並びの語を受け、同じ出力にしてある。 */
#include "gpiodev.h"

static unsigned put_str(char *out, unsigned max, unsigned n, const char *s)
{
    while (*s && n + 1 < max)
        out[n++] = *s++;
    out[n] = 0;
    return n;
}

static unsigned put_num(char *out, unsigned max, unsigned n, unsigned v)
{
    char t[12];
    int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i && n + 1 < max)
        out[n++] = t[--i];
    out[n] = 0;
    return n;
}

int gpio_name_to_pin(const char *c)
{
    unsigned v = 0;
    if (c[0] != 'g' || c[1] != 'p' || c[2] != 'i' || c[3] != 'o')
        return -1;
    c += 4;
    if (*c == 0 || *c == '/')
        return GPIO_ALL;
    if (*c < '0' || *c > '9')
        return -1;
    while (*c >= '0' && *c <= '9' && v < 1000)
        v = v * 10 + (unsigned)(*c++ - '0');
    if (*c != 0 && *c != '/')
        return -1;
    return (int)v;
}

int gpio_read(unsigned pin, char *out, unsigned max)
{
    unsigned n = 0, p;
    const char *why;

    out[0] = 0;
    if (pin == GPIO_ALL) {
        n = put_str(out, max, n, "pin  dir  val\n");
        for (p = 0; p < gpio_npins(); p++) {
            if (pin_check(p, 0))
                continue;
            n = put_num(out, max, n, p);
            n = put_str(out, max, n, p < 10 ? "    " : "   ");
            n = put_str(out, max, n, pin_is_output(p) ? "out" : "in ");
            n = put_str(out, max, n, "  ");
            n = put_num(out, max, n, (unsigned)pin_get(p));
            n = put_str(out, max, n, "\n");
        }
        return (int)n;
    }
    if ((why = pin_check(pin, 0)) != 0) {
        n = put_str(out, max, n, "gpio: ");
        n = put_str(out, max, n, why);
        n = put_str(out, max, n, "\n");
        return (int)n;
    }
    n = put_num(out, max, n, (unsigned)pin_get(pin));
    n = put_str(out, max, n, "\n");
    return (int)n;
}

/* 空白で区切って最大 3 語。line は書き換える */
static int split(char *s, char **w, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
            *s++ = 0;
        if (!*s)
            break;
        w[n++] = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r')
            s++;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        *s++ = 0;
    return n;
}

static int fail(char *out, unsigned max, const char *why)
{
    unsigned n = put_str(out, max, 0, "gpio: ");
    put_str(out, max, n, why);
    return 1;
}

int gpio_exec(int pin, const char *line, char *out, unsigned max)
{
    char buf[40], *w[3];
    int i, argc;
    unsigned p, v = 0;
    const char *why, *cmd;

    for (i = 0; line[i] && i < (int)sizeof(buf) - 1; i++)
        buf[i] = line[i];
    buf[i] = 0;
    argc = split(buf, w, 3);
    out[0] = 0;

    if (pin < 0) {                                   /* PIN は語の先頭 */
        if (argc == 0)
            return gpio_read(GPIO_ALL, out, max) < 0;
        if (w[0][0] < '0' || w[0][0] > '9')
            return fail(out, max, "usage: [PIN [0|1|toggle|in [up|down]]]");
        while (*w[0] >= '0' && *w[0] <= '9' && v < 1000)
            v = v * 10 + (unsigned)(*w[0]++ - '0');
        p = v;
        w[0] = w[1]; w[1] = w[2]; argc--;            /* 語を 1 つ詰める */
    } else {
        p = (unsigned)pin;
    }
    if (argc == 0) {                                 /* 読む */
        if ((why = pin_check(p, 0)) != 0) return fail(out, max, why);
        gpio_read(p, out, max);
        return 0;
    }
    cmd = w[0];
    if (cmd[0] == 'i') {                             /* in [up|down] */
        int pull = 0;
        if ((why = pin_check(p, 0)) != 0) return fail(out, max, why);
        if (argc >= 2) pull = w[1][0] == 'u' ? 1 : w[1][0] == 'd' ? 2 : 0;
        pin_output(p, 0);
        pin_pull(p, pull);
        return 0;
    }
    if ((why = pin_check(p, 1)) != 0) return fail(out, max, why);
    if (cmd[0] == 't') {                             /* toggle */
        int nv = pin_is_output(p) ? !pin_get(p) : 1;
        pin_set(p, nv);
        pin_output(p, 1);
        put_num(out, max, 0, (unsigned)nv);
        put_str(out, max, 1, "\n");
        return 0;
    }
    if ((cmd[0] == '0' || cmd[0] == '1') && cmd[1] == 0) {
        pin_set(p, cmd[0] == '1');                   /* 値を先に置いてから出力にする(ひげを出さない) */
        pin_output(p, 1);
        return 0;
    }
    return fail(out, max, "usage: [PIN [0|1|toggle|in [up|down]]]");
}
