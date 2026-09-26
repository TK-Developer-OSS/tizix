#include <stdio.h>
#include <ctype.h>

/* ctype 系ランタイム関数。crt0.s(ベアメタル) と x86(libc) で同一出力。
   libc の isXXX は「非ゼロ」を返すだけで 1 とは限らないので b() で 0/1 化する。 */
int b(int x) { return x ? 1 : 0; }

int main(int argc, char *argv[]) {
    printf("%d%d%d%d\n", b(isdigit('7')), b(isdigit('x')), b(isalpha('A')), b(isalpha('9')));
    printf("%d%d\n", b(isspace(' ')), b(isspace('.')));
    printf("%c%c\n", toupper('a'), tolower('Z'));
    printf("%d%d\n", b(isxdigit('f')), b(isxdigit('g')));
    printf("%d%d\n", b(isupper('Q')), b(islower('q')));
    printf("%d%d%d\n", b(isalnum('z')), b(isalnum('5')), b(isalnum('-')));
    printf("%d%d\n", b(ispunct('!')), b(ispunct('A')));
    return 0;
}

// EXPECT: 1010
// EXPECT: 10
// EXPECT: Az
// EXPECT: 10
// EXPECT: 11
// EXPECT: 110
// EXPECT: 10
