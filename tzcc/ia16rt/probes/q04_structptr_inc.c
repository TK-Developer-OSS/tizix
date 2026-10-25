struct e { char c; int v; };
struct e tab[3];
int main(void) { struct e *p; int s; tab[0].v = 1; tab[1].v = 2; tab[2].v = 3; p = tab; s = 0; p++; s = p->v; p++; s = s * 10 + p->v; return s; }
