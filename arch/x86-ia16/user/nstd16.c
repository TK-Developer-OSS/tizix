/* arch/x86-ia16/user/nstd16.c -- ネット系コマンド(atcli / tzftp)が使う stdlib の最小実装
 *   z80 は stdlib.rel の手書きアセンブラ。x86 は ESP-AT 版(netcli16.c)・モデム版(netmodem16.c)の両方から切り離して持つ。 */
#include "tzstdio.h"

/* atcli / tzftp が使う stdlib の最小実装(z80 は stdlib.rel の手書きアセンブラ。x86 はネット系コマンドにだけ持たせる) */
int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

char *itoa(int value, char *str, int radix)
{
    char t[17];
    int i = 0, j = 0, neg = 0;
    unsigned u;
    if (radix < 2 || radix > 16) radix = 10;
    if (value < 0 && radix == 10) { neg = 1; u = (unsigned)(-value); } else u = (unsigned)value;
    do { unsigned d = u % (unsigned)radix; t[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)radix; } while (u);
    if (neg) str[j++] = '-';
    while (i) str[j++] = t[--i];
    str[j] = 0;
    return str;
}
