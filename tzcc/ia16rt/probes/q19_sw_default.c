int f(int k) { switch (k) { case 1: return 10; case 2: return 20; default: return 99; } }
int main(void) { return f(2) + f(7); }
