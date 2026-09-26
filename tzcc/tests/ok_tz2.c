

/* for ループ: 0..9 を出力 */
int main(int argc, char *argv[]) {
    int i = 0;
    for (i = 0; i < 10; i = i + 1) {
        putchar(48 + i);
    }
    putchar(10);
    return 0;
}

// EXPECT: 0123456789
