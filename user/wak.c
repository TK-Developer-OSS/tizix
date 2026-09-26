/* user/wak.c - 5a 検証用(一時)。`wak <blk>` で指定ブロックを runnable に戻す。
 *   引数は argv[0] に "<数字>" 文字列全体(crt0cmd 規約)。 */
#include "stdio.h"

int main(int argc, char **argv)
{
    unsigned char n = 0;
    const char *p;

    (void)argc;
    p = (argv && argv[0]) ? argv[0] : "";
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') {
        n = (unsigned char)(n * 10 + (*p - '0'));
        p++;
    }
    proc_wake(n);
    printf("wak: block %u\n", (unsigned)n);
    return 0;
}
