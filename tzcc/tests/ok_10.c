#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char *s1 = "Hello ";
    char *s2 = "TzCC";
    char buffer[32];
    
    strcpy(buffer, s1);
    strcat(buffer, s2);
    puts(buffer);
    
    return 0;
}

// EXPECT: Hello TzCC
