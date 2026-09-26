/* user/blk.c - 5a 検証用(一時)。proc_block で自プロセスを park し、
 *   別プロセスの `wak <blk>` に起こされるまで戻らないことを見る。
 *   起こされたら 1 行出して exit。 */
#include "stdio.h"

/* KW_CURRENT(現走行ブロック番号)。フラットメモリなので直読み可(kmem.h) */
#define KW_CURRENT (*(volatile unsigned char *)0x8418)

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("blk: parking block %u\n", (unsigned)KW_CURRENT);
    proc_block();
    puts("blk: woke, exit");
    return 0;
}
