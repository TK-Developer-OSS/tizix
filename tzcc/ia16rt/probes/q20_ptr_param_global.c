char g[4];
void fill(char *p, int n) { int i; for (i = 0; i < n; i++) p[i] = i + 1; }
int main(void) { fill(g, 4); return g[0] + g[3]; }
