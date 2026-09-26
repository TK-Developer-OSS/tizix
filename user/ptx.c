/* user/ptx.c - 5b 検証用(一時)。パイプ writer。
 *   `ptx N | prx` で N 行 "line-K" を stdout(= カーネルパイプ)へ書く。
 *   putchar/printf はパイプを意識しない(kputchar が ROUTE_PIPE で振り分け)。 */
#include "stdio.h"

int main(int argc, char **argv)
{
    unsigned int n = 0, i;
    const char *p;

    (void)argc;
    p = (argv && argv[0]) ? argv[0] : "";
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') { n = (unsigned int)(n * 10 + (*p - '0')); p++; }
    if (n == 0) n = 3;

    for (i = 0; i < n; i++)
        printf("line-%u\n", i);

    return 0;
}
