#include <stdio.h>
/* block comment before code
   spanning multiple lines */

int main(int argc, char *argv[]) {   // line comment after code
    char *msg = "Comment test OK\n";
    /* inline */ printf(msg); // trailing
    // whole line comment
    return 0; /* comment before semicolon already consumed */
}

// EXPECT: Comment test OK
