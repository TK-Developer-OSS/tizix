int main(void) {
    char s[4]; char d[4]; char *p; char *q; int n;
    s[0] = 7; s[1] = 8; s[2] = 0;
    p = s; q = d; n = 0;
    for (;;) { *q = *p; if (*q == 0) break; p++; q++; n++; }
    return n;
}
