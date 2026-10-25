char a[8]; char b[8];
int main(void) { char *p; char *q; int n; a[0]=5; a[1]=6; a[2]=0; p = a; q = b; n = 0; while (*p) { *q = *p; p++; q++; n++; } *q = 0; return n * 100 + b[1]; }
