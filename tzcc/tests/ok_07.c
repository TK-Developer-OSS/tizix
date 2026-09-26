#include <stdio.h>

char* get_greeting(char *name) {
    return "Hello ";
}

void print_both(char *str1, char *str2) {
    printf(str1);
    printf(str2);
}

int main(int argc, char *argv[]) {
    char *g = get_greeting("test");
    char[] msg = "TzCC\n";
    print_both(g, msg);
    return 0;
}
// EXPECT: Hello TzCC
