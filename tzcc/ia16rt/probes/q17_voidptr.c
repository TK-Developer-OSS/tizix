char buf[4];
int main(void) { void *p; p = buf; ((char *)p)[1] = 9; return buf[1]; }
