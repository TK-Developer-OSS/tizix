/* Tizix bring-up: putchar のみ。crt0_tizix の最小 libc で動く。
   期待出力: OK<改行> */
int main(int argc, char *argv[]) {
    putchar(79);   /* 'O' */
    putchar(75);   /* 'K' */
    putchar(10);
    return 0;
}

// EXPECT: OK
