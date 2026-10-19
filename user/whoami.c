/* user/whoami.c - 外部コマンド whoami
 *   whoami : 現在のユーザー名を表示する。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("root");
    return 0;
}