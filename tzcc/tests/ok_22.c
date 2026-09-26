#include <stdio.h>

/* int を2バイトで扱えること: 500 は char(1byte) には収まらない。
   int なら '.' が丁度 5 個出る（500,400,300,200,100 > 0）。 */
int main(int argc, char *argv[]) {
    int n = 500;
    while (n > 0) {
        putchar(46);   /* '.' */
        n = n - 100;
    }
    putchar(10);
    return 0;
}

// EXPECT: .....
