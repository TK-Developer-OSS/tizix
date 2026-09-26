/* user/prx.c - 5b 検証用(一時)。パイプ reader。
 *   stdin(= カーネルパイプ)を EOF まで読み、各行頭に "rx: " を付けて stdout へ。
 *   getchar はパイプを意識しない(kgetchar が pipe_is_reader で振り分け、
 *   空なら proc_block、writer 終了+空で -1)。 */
#include "stdio.h"

int main(int argc, char **argv)
{
    int c;
    unsigned char at_bol = 1;

    (void)argc;
    (void)argv;

    while ((c = getchar()) != -1) {
        if (at_bol) { printf("rx: "); at_bol = 0; }
        putchar(c);
        if (c == '\n') at_bol = 1;
    }
    printf("prx: EOF\n");
    return 0;
}
