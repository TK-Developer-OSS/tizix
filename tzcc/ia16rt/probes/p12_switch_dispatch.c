static int r0(int a) { return a; }
static int r1(int a) { return a + 100; }
static int disp(int k, int a) { switch (k) { case 0: return r0(a); case 1: return r1(a); } return -1; }
int main(void) { return disp(1, 5); }
