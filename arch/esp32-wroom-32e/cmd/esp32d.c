/* esp32d(/bin/esp32d.bin、#112): ESP32 の周辺機器を持つ常駐プロセス。
 *
 *   郵便受け "esp32d"(user/mbox.h)で依頼を受け、装置ごとの処理(gpio.c / pwm.c / adc.c /
 *   i2c.c / spi.c の dev_xxx)を呼んで、表示する文字列を返事で返す。約束は cmd/esp32d.h。
 *   起こし方: /etc/rc の `esp32d &`(要らなければその行をコメントに)。手で `esp32d &` してもよい
 *   (二重には起きない)。居ないとき esp32-* は「動いていない」と言って終わる。
 *   依頼は 1 つずつ順に処理する。長い依頼(ADC の 1000 回読みなど)の間、次の依頼は待たされる。
 */
#include "esp32d.h"
#include "mbox.h"

static char rep[ESP32D_REPMAX];
static unsigned rlen;

static void rput(int c)
{
    if (rlen < sizeof rep)
        rep[rlen++] = (char)c;
}

static void rputs(const char *s)
{
    while (*s) rput((unsigned char)*s++);
}

static void rnum(unsigned long v, unsigned base)
{
    char b[11];
    int i = 0;
    do {
        unsigned d = (unsigned)(v % base);
        b[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (i) rput(b[--i]);
}

/* user/stdio.h の printf と同じ書式(%s %d %u %c %x %%)を、返事の文字列へ積む */
#undef printf
int rprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { rput((unsigned char)*fmt); continue; }
        switch (*++fmt) {
        case 's': rputs(__builtin_va_arg(ap, const char *)); break;
        case 'd': {
            int d = __builtin_va_arg(ap, int);
            if (d < 0) { rput('-'); d = -d; }
            rnum((unsigned)d, 10);
            break;
        }
        case 'u': rnum(__builtin_va_arg(ap, unsigned), 10); break;
        case 'x': rnum(__builtin_va_arg(ap, unsigned), 16); break;
        case 'c': rput(__builtin_va_arg(ap, int)); break;
        case '%': rput('%'); break;
        default:  rput('%'); rput((unsigned char)*fmt); break;
        }
    }
    __builtin_va_end(ap);
    return 0;
}

static const struct {
    const char *name;
    int (*fn)(int, char **);
} devs[] = {
    { "gpio", dev_gpio }, { "pwm", dev_pwm }, { "adc", dev_adc },
    { "i2c",  dev_i2c },  { "spi", dev_spi }, { "rgb", dev_rgb },
};

#define MAXARG 16

int main(void)
{
    static char req[ESP32D_REQMAX + 1];
    char *av[MAXARG + 1];
    int port, ac;
    unsigned long r;
    unsigned i, n, k;

    port = mb_bind(ESP32D_NAME);
    if (port < 0)
        return 1;           /* もう居る。黙って終わる(sh を exit すると init が sh を起こし直し、
                             * /etc/rc がもう一度走るので、背景から文言が混ざらないように) */
    klog("esp32d: started");
    for (;;) {
        r = mb_recv(port, req, ESP32D_REQMAX, MB_FOREVER);
        if (r == 0 || r == MB_ERR)
            continue;
        n = MB_LEN(r);
        req[n] = 0;
        /* "装置名\0引数…" を語に分ける。av[0] = 装置名 */
        ac = 0;
        for (i = 0; i < n && ac < MAXARG; ) {
            av[ac++] = req + i;
            while (i < n && req[i]) i++;
            i++;
        }
        av[ac] = 0;

        rlen = 1;
        rep[0] = 1;
        for (k = 0; k < sizeof devs / sizeof devs[0]; k++)
            if (ac > 0 && strcmp(av[0], devs[k].name) == 0) {
                rep[0] = (char)devs[k].fn(ac - 1, av + 1);
                break;
            }
        if (k == sizeof devs / sizeof devs[0])
            rprintf("esp32d: unknown device %s\n", ac > 0 ? av[0] : "");
        mb_reply(MB_FROM(r), rep, rlen);
    }
}
