// ptrarr[i][j] は char（1バイト読み）であること。
// バグ時は 2バイト(z80)/8バイト(x86) 読みになり、w[1][0]=='-' などの
// option 解析パターンが全滅する（head -N / tail -N の土台）。
// EXPECT: dash -n ok 0 1 2
#include <stdio.h>

static void pr(char *s) {
    int i;
    for (i = 0; s[i]; i = i + 1) putchar(s[i]);
    putchar(' ');
}

int main(int argc, char **argv) {
    char a0[8];
    char a1[8];
    char *w[3];
    (void)argc; (void)argv;

    a0[0] = 'd'; a0[1] = 'a'; a0[2] = 's'; a0[3] = 'h'; a0[4] = 0;
    a1[0] = '-'; a1[1] = 'n'; a1[2] = 0;

    w[0] = a0;
    w[1] = a1;
    w[2] = a0;

    pr(w[0]);                                   /* dash  */
    pr(w[1]);                                   /* -n    */

    putchar(w[1][0] == '-' ? 'o' : 'x');
    putchar(w[1][1] == 'n' ? 'k' : 'x');
    putchar(' ');
    putchar(w[0][0] == 'd' ? '0' : 'x'); putchar(' ');
    putchar(w[1][0] == '-' ? '1' : 'x'); putchar(' ');
    putchar(w[2][3] == 'h' ? '2' : 'x');
    putchar('\n');
    return 0;
}
