#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Tizix 実機用: crt0_tizix.s に追加した string / ctype / stdlib / printf の確認。
   make tizix TEST_SRC=tests/ok_tz4.c → './cpmsim -z' の '# ' で OKTZ4 */
int main(int argc, char *argv[]) {
    char buf[32];

    strcpy(buf, "tizix");
    strcat(buf, "!");
    printf("len=%d\n", strlen(buf));                       /* len=6 */
    printf("up=%c val=%d hex=%x\n", toupper('a'), 255, 255);/* up=A val=255 hex=ff */

    itoa(-1234, buf, 10);
    printf("itoa=%s\n", buf);                              /* itoa=-1234 */
    printf("atoi=%d\n", atoi("  42abc"));                  /* atoi=42 */
    printf("cmp=%d\n", strcmp("ab", "ac"));                /* cmp=-1 */
    printf("dig=%d alpha=%d\n", isdigit('7'), isalpha('7'));/* dig=1 alpha=0 */
    puts("s=done");                                        /* s=done */
    return 0;
}

// EXPECT: len=6
// EXPECT: up=A val=255 hex=ff
// EXPECT: itoa=-1234
// EXPECT: atoi=42
// EXPECT: cmp=-1
// EXPECT: dig=1 alpha=0
// EXPECT: s=done
// RUN: skip Tizix 実機専用 (make tizix)
