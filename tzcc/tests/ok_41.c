#include <stdio.h>
#include <stdlib.h>

/* itoa / atoi の往復。itoa は glibc に無いので x86 実行はスキップ。 */
int main(int argc, char *argv[]) {
    char buf[16];

    itoa(12345, buf, 10);
    printf("%s\n", buf);           /* 12345 */
    itoa(-42, buf, 10);
    printf("%s\n", buf);           /* -42 */
    itoa(255, buf, 16);
    printf("%s\n", buf);           /* ff */
    itoa(0, buf, 10);
    printf("%s\n", buf);           /* 0 */

    printf("%d\n", atoi("  -123abc"));   /* -123 */
    printf("%d\n", atoi("6789"));        /* 6789 */
    printf("%d\n", atoi(buf) + 7);       /* buf=="0" -> 7 */
    return 0;
}

// RUN: skip-x86 itoa is not in glibc
// EXPECT: 12345
// EXPECT: -42
// EXPECT: ff
// EXPECT: 0
// EXPECT: -123
// EXPECT: 6789
// EXPECT: 7
