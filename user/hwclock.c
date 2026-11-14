/* user/hwclock.c - external command hwclock (MSX2 only; /etc/rc runs it at boot)
 *
 *   Sets the kernel's Unix time from the real-time clock (RP-5C01).  The boot loader (s2.s)
 *   already copied the clock's registers to 0x83CB: 'R','T', then block 0 registers 0-12
 *   (one nibble each: offset 2 + register), then block 1 register 10 (offset 15) (bit 0 = 24-hour mode).  Without the 'R','T'
 *   marker (other machines) or with nonsense in the fields, it does nothing.
 *
 *   The MSX clock counts years from 1980 (0..99).  No division, no multiplication by a
 *   variable (the tzcc rules): months and years are walked, 32-bit seconds are added up.
 */
#include "stdio.h"

#define RTADDR  ((volatile unsigned char *)0x83CB)
#define EPOCH   ((volatile unsigned long *)0x8522)

static unsigned char mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static unsigned char two(unsigned char tens, unsigned char ones)
{
    return (unsigned char)((tens << 3) + (tens << 1) + ones);
}

int main(int argc, char **argv)
{
    unsigned char sec, min, hour, day, mon, yy, i, a, b;
    unsigned y, days;
    unsigned long t, d;
    volatile unsigned char *RT;
    volatile unsigned long *ep;

    RT = RTADDR;                    /* indexing the cast constant directly miscompiles in tzcc */
    (void)argc;
    (void)argv;
    a = RT[0];
    b = RT[1];
    if (a != 0x52 || b != 0x54) {              /* 'R', 'T' */
        puts("hwclock: no clock");
        return 1;
    }
    a = RT[3] & 7;  b = RT[2];  sec = two(a, b);
    a = RT[5] & 7;  b = RT[4];  min = two(a, b);
    b = RT[6];
    if (RT[15] & 1) {                                 /* 24-hour mode */
        a = RT[7] & 3;
        hour = two(a, b);
    } else {                                          /* 12-hour mode, bit 1 = PM */
        a = RT[7] & 1;
        hour = two(a, b);
        if (hour == 12) hour = 0;
        if (RT[7] & 2) hour += 12;
    }
    a = RT[10] & 3; b = RT[9];  day = two(a, b);
    a = RT[12] & 1; b = RT[11]; mon = two(a, b);
    a = RT[14];     b = RT[13]; yy  = two(a, b);
    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 || mon > 12 || yy > 99) {
        puts("hwclock: bad clock");
        return 1;
    }

    days = 0;
    for (y = 1970; y < 1980u + yy; y++)
        days += (y & 3) ? 365 : 366;
    for (i = 1; i < mon; i++) {
        days += mdays[i - 1];
        if (i == 2 && !((1980u + yy) & 3)) days++;
    }
    days += day - 1;

    /* seconds = days * 86400 + hour * 3600 + min * 60 + sec.  tzcc's 32-bit shifts and
     * multiplies miscompile and a 20000-step loop is far too slow, so: 86400 = 675 << 7 and
     * days * 675 by doubling and adding (675 = 0b1010100011, top bit first), then 7 doublings. */
    d = days;
    t = 0;
    for (i = 0; i < 10; i++) {
        t = t + t;
        if ((675u >> (9 - i)) & 1) t = t + d;
    }
    for (i = 0; i < 7; i++) t = t + t;
    for (i = 0; i < hour; i++) t += 3600UL;
    for (i = 0; i < min; i++)  t += 60UL;
    t += sec;
    ep = EPOCH;
    *ep = t;
    return 0;
}
