#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char buf[10];
    memset(buf, 'A', 9);
    buf[9] = '\0';
    puts(buf);
    
    char src[] = "CopyTest";
    char dst[10];
    memcpy(dst, src, 8);
    dst[8] = '\0';
    puts(dst);
    
    return 0;
}

// EXPECT: AAAAAAAAA
// EXPECT: CopyTest
