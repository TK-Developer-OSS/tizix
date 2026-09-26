#include <stdio.h>

/* int の引数・戻り値が 2バイトで受け渡しできること。
   add(200,100)=300。300/100 で '#' が 3 個出る。 */
int add(int a, int b) {
    return a + b;
}

int main(int argc, char *argv[]) {
    int r = add(200, 100);
    int k = 0;
    for (k = 0; k < r; k = k + 100) {
        putchar(35);   /* '#' */
    }
    putchar(10);
    return 0;
}

// EXPECT: ###
