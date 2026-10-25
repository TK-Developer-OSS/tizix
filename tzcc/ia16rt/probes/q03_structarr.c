struct e { int k; int v; };
struct e tab[3];
int main(void) { int i; int s; for (i = 0; i < 3; i++) { tab[i].k = i; tab[i].v = i * 10; } s = 0; for (i = 0; i < 3; i++) s = s + tab[i].v + tab[i].k; return s; }
