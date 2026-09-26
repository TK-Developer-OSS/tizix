#include <stdio.h>
#include <string.h>

/* 追加した string / mem 系ランタイム関数の動作確認。
   crt0.s(ベアメタル) と x86(libc) の両方で同じ出力になること。 */
int main(int argc, char *argv[]) {
    char buf[32];
    char *p;
    char *q;

    strcpy(buf, "Hello");
    strcat(buf, ", World");
    printf("%s\n", buf);              /* Hello, World */
    printf("%d\n", strlen(buf));      /* 12 */

    p = strchr(buf, 'W');
    printf("%s\n", p);               /* World */
    p = strrchr(buf, 'l');
    printf("%s\n", p);               /* ld */

    printf("%d\n", strcmp("abc", "abc"));   /* 0 */

    strncpy(buf, "abcdef", 3);
    buf[3] = 0;
    printf("%s\n", buf);            /* abc */

    memset(buf, 'x', 4);
    buf[4] = 0;
    printf("%s\n", buf);            /* xxxx */

    strcpy(buf, "0123456789");
    q = buf;
    memmove(q + 2, q, 5);          /* 重なりコピー: "01" + "01234" + "789" */
    buf[9] = 0;
    printf("%s\n", buf);           /* 010123478 */

    return 0;
}

// EXPECT: Hello, World
// EXPECT: 12
// EXPECT: World
// EXPECT: ld
// EXPECT: 0
// EXPECT: abc
// EXPECT: xxxx
// EXPECT: 010123478
