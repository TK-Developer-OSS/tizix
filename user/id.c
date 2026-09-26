/* user/id.c - 外部コマンド id
 *   id : ユーザー情報（固定）を表示する。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("uid=0(root) gid=0(root)");
    return 0;
}