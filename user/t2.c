/* t2.c - isolate lstd.s long-return functions + drv_printf %ld/%lu 検証 */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("t2 start");

    long a = labs(-12345L);
    puts("after labs");
    printf("labs      = %ld\n", a);

    long b = atol("  -321");
    puts("after atol");
    printf("atol(-321)= %ld\n", b);

    long c = atol("100");
    printf("atol(100) = %ld\n", c);

    /* drv_printf %ld / %lu を直接検証（16bit 桁上げ・負値・ゼロ・大値） */
    printf("zero      = %ld\n", 0L);
    printf("neg       = %ld\n", -1L);
    printf("big       = %ld\n", 100000L);       /* > 65535, 16bit 桁上がり */
    printf("huge      = %ld\n", 2000000000L);
    printf("ulong max = %lu\n", 4294967295UL);
    printf("mixed %d / %ld / %s\n", 42, 123456L, "end");

    puts("t2 done");
    return 0;
}
