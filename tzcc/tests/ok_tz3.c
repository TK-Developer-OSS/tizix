/* Tizix: 配列添字 a[i] の読み書きを putchar だけで検証 */
int main(int argc, char *argv[]) {
    char buf[8];
    int i = 0;
    for (i = 0; i < 5; i = i + 1) {
        buf[i] = 65 + i;       /* 書き込み 'A'..'E' */
    }
    for (i = 0; i < 5; i = i + 1) {
        putchar(buf[i]);       /* 読み出し */
    }
    putchar(10);
    return 0;
}

// EXPECT: ABCDE
