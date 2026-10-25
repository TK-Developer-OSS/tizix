int f(int a, int b) { if (a && b) return 1; if (a || b) return 2; return 3; }
int main(void) { int t; t = f(1,1)*100 + f(0,1)*10 + f(0,0); return t > 200 ? t - 200 : t; }
