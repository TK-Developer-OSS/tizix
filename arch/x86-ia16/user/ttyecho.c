/* arch/x86-ia16/user/ttyecho.c -- test for the DRIVER's serial port (task #245): opens /dev/ttyS1 (COM2), sends
 *   "READY", then echoes every received byte back for N seconds (default 10) and prints how many came.
 *   usage: ttyecho [seconds]
 */
#include "tzstdio.h"

int tty_open(int unit, int divisor);
int tty_close(void);
int tty_getc(void);
int tty_putc(int c);
int tty_avail(void);
int tty_drops(void);

int main(int argc, char **argv)
{
    unsigned secs = 10, t0, n = 0, i;
    int c;
    const char *m = "READY\r\n";

    if (argc > 0 && argv[0] && argv[0][0]) {      /* argv[0] is the first argument (tizix commands get no program name) */
        secs = 0;
        for (i = 0; argv[0][i]; i++)
            secs = secs * 10 + (argv[0][i] - '0');
    }
    if (tty_open(1, 1) != 0) {
        printf("ttyecho: ttyS1 is busy\n");
        return 1;
    }
    for (i = 0; m[i]; i++)
        tty_putc(m[i]);
    t0 = getticks();
    while ((unsigned)(getticks() - t0) < secs * 100u) {
        c = tty_getc();
        if (c < 0)
            continue;
        tty_putc(c);
        n++;
    }
    printf("ttyecho: %u bytes, %u lost\n", n, (unsigned)tty_drops());
    tty_close();
    return 0;
}
