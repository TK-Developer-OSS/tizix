/* user/uname.c - 外部コマンド uname
 *   uname : システム情報を表示する。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("tizix 1.0");
    return 0;
}