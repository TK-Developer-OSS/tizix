char buf[16];
int main(void) { char *p; char *q; p = buf; q = buf + 5; return q - p; }
