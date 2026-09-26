#include <stdio.h>
#include <string.h>

/* strstr / memchr。crt0.s(ベアメタル) と x86(libc) で同一出力。 */
int main(int argc, char *argv[]) {
    char *s;
    char *p;

    s = "the quick brown fox";
    p = strstr(s, "quick");
    printf("%s\n", p);              /* quick brown fox */
    p = strstr(s, "brown fox");
    printf("%s\n", p);              /* brown fox */
    printf("%d\n", strstr(s, "cat") == 0);   /* 1 (見つからない) */
    p = strstr(s, "");
    printf("%d\n", p == s);         /* 1 (空パターンは先頭) */

    p = memchr(s, 'b', 19);
    printf("%s\n", p);              /* brown fox */
    printf("%d\n", memchr(s, 'z', 19) == 0);  /* 1 */

    return 0;
}

// EXPECT: quick brown fox
// EXPECT: brown fox
// EXPECT: 1
// EXPECT: 1
// EXPECT: brown fox
// EXPECT: 1
