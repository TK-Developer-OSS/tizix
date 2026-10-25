static int a1(int x) { return x + 1; }
static int a2(int x) { return x + 2; }
int main(void) {
    int (*fp)(int);
    fp = a2;
    return fp(5);
}
