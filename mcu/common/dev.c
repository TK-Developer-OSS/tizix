/* dev.c - the /dev tree. A fixed table of device nodes, not a file system:
 *   /dev/gpio/N            echo 0|1|in|up > ...     cat ...  (the pin level)
 *   /dev/timer/0/limit     echo MS > ...            ms until the timer finishes; 0 stops it
 *   /dev/timer/0/finish    echo 0 > ... clears it   cat ...  (1 once the limit was reached)
 *   /dev/timer/0/repeat    echo 1 > ...             restart after each finish (default 0: one shot)
 * The chip hooks do the real work (mcu.h); this file only knows the names. */
#include <string.h>
#include "mcu.h"

enum { N_NONE, N_GPIO, N_LIMIT, N_FINISH, N_REPEAT };

/* path -> node id (and the pin number for N_GPIO) */
static uint8_t resolve(const char *p, unsigned *n)
{
    if (mstrncmp_P(p, P("/dev/"), 5) != 0)
        return N_NONE;
    p += 5;
    if (mstrncmp_P(p, P("gpio/"), 5) == 0) {
        if (mparse_uint(p + 5, n) != 0 || !hal_gpio_valid(*n))
            return N_NONE;
        return N_GPIO;
    }
    if (mstrcmp_P(p, P("timer/0/limit")) == 0)
        return N_LIMIT;
    if (mstrcmp_P(p, P("timer/0/finish")) == 0)
        return N_FINISH;
    if (mstrcmp_P(p, P("timer/0/repeat")) == 0)
        return N_REPEAT;
    return N_NONE;
}

static int no_such(const char *who, const char *path)   /* who: flash string */
{
    mputs_P(who);
    mputs_P(P(": "));
    mputs(path);
    mputs_P(P(": no such device\n"));
    return 1;
}

int dev_read(const char *path, char *buf, uint8_t max)
{
    unsigned n = 0, v;
    uint8_t id = resolve(path, &n), i = 0;
    char t[6];

    if (id == N_NONE)
        return 1;
    v = id == N_GPIO ? (unsigned)hal_gpio_read(n)
                     : hal_timer_get(id == N_LIMIT ? TIMER_LIMIT : id == N_FINISH ? TIMER_FINISH : TIMER_REPEAT);
    do {
        t[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (n = 0; i && n + 1 < max; n++)
        buf[n] = t[--i];
    buf[n] = 0;
    return 0;
}

int dev_cat(const char *path)
{
    char v[8];

    if (dev_read(path, v, sizeof(v)) != 0)
        return no_such(P("cat"), path);
    mputs(v);
    mputc('\n');
    return 0;
}

int dev_write(const char *path, const char *word)
{
    unsigned n = 0, v;
    uint8_t id = resolve(path, &n);

    if (id == N_NONE)
        return no_such(P("sh"), path);
    if (id == N_GPIO) {
        if (hal_gpio_write(n, word) != 0) {
            mputs_P(P("sh: bad value for a gpio pin (0, 1, in, up)\n"));
            return 1;
        }
        return 0;
    }
    if (mparse_uint(word, &v) != 0 ||
        hal_timer_set(id == N_LIMIT ? TIMER_LIMIT : id == N_FINISH ? TIMER_FINISH : TIMER_REPEAT, v) != 0) {
        mputs_P(P("sh: bad value\n"));
        return 1;
    }
    return 0;
}

/* ls is recursive: it lists everything below a path as full paths, in columns that run down and then
 * across, like Linux ls (80 columns). The tree is small and fixed, so every entry is generated here;
 * the generator runs once to count and measure, then once per printed entry. A directory ends in '/'. */
#define COUNT 0xFF
static uint8_t listed, want, maxlen, total, rows, colw;

static void show(const char *p, const char *e, uint8_t dir)
{
    uint8_t n = (uint8_t)strlen(p), l = (uint8_t)strlen(e), m = l - dir;   /* m: e without its trailing '/' */

    if (n != 0) {                                       /* "/" (n == 0) shows everything */
        if (strncmp(e, p, n) != 0)
            return;
        if (m == n) {                                   /* the path itself: only a device, not a directory */
            if (dir)
                return;
        } else if (e[n] != '/') {
            return;
        }
    }
    if (want == COUNT) {
        if (l > maxlen)
            maxlen = l;
    } else if (listed == want) {
        mputs(e);
        if ((uint8_t)(listed + rows) < total)           /* not the last column of its row */
            for (; l < colw; l++)
                mputc(' ');
    }
    listed++;
}

static void walk(const char *p)
{
    char e[24], nm[16];
    uint8_t i, k;
    unsigned n;

    listed = 0;
    mstrcpy_P(e, P("/bin/"));
    show(p, e, 1);
    for (k = 0; cmd_name(k, nm); k++) {                 /* the builtin commands are in /bin */
        mstrcpy_P(e, P("/bin/"));
        strcpy(e + 5, nm);
        show(p, e, 0);
    }
    for (k = 0; script_name(k, nm); k++) {              /* the baked-in scripts are in / */
        e[0] = '/';
        strcpy(e + 1, nm);
        show(p, e, 0);
    }
    mstrcpy_P(e, P("/dev/"));
    show(p, e, 1);
    mstrcpy_P(e, P("/dev/gpio/"));
    show(p, e, 1);
    for (n = 0; n < 32; n++)
        if (hal_gpio_valid(n)) {
            mstrcpy_P(e, P("/dev/gpio/"));
            i = (uint8_t)strlen(e);
            if (n >= 10)
                e[i++] = (char)('0' + n / 10);
            e[i++] = (char)('0' + n % 10);
            e[i] = 0;
            show(p, e, 0);
        }
    mstrcpy_P(e, P("/dev/timer/"));
    show(p, e, 1);
    mstrcpy_P(e, P("/dev/timer/0/"));
    show(p, e, 1);
    mstrcpy_P(e, P("/dev/timer/0/limit"));
    show(p, e, 0);
    mstrcpy_P(e, P("/dev/timer/0/finish"));
    show(p, e, 0);
    mstrcpy_P(e, P("/dev/timer/0/repeat"));
    show(p, e, 0);
}

int dev_ls(const char *path)
{
    char p[24];
    uint8_t len = (uint8_t)strlen(path), cols, r, i;

    if (len >= sizeof(p))
        len = sizeof(p) - 1;
    memcpy(p, path, len);
    p[len] = 0;
    while (len > 0 && p[len - 1] == '/')                /* "/dev/" is "/dev", "/" is "" */
        p[--len] = 0;

    want = COUNT; maxlen = 0; total = 0; rows = 0;
    walk(p);
    total = listed;
    if (total == 0)
        return no_such(P("ls"), path);
    colw = maxlen + 2;
    cols = 80 / colw;
    if (cols == 0)
        cols = 1;
    rows = (total + cols - 1) / cols;
    for (r = 0; r < rows; r++) {
        for (i = r; i < total; i += rows) {
            want = i;
            walk(p);
        }
        mputc('\n');
    }
    return 0;
}
