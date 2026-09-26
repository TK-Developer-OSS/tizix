/* user/echo.c - 外部コマンド echo
 *   argv[0..argc-1] を空白区切りで出力し、末尾に改行。
 *   sh が行をトークン化して argv[] を渡す(crt0cmd の argv[] ABI)。
 *   `echo TEXT > FILE` は sh が redir_begin → 前景実行 → redir_end で
 *   putchar をファイルへ載せる。
 *   掟: argc は符号付き int なので unsigned へ写してから比較する。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    unsigned ac = (unsigned)argc;
    unsigned i;

    for (i = 0; i < ac; i++) {
        const char *s = argv[i];
        while (s && *s)
            putchar(*s++);
        if (i + 1u < ac)
            putchar(' ');
    }
    putchar('\n');
    return 0;
}
