struct nm { char name[6]; int t; };
struct nm n1;
int main(void) { struct nm *p; p = &n1; p->name[0] = 'a'; p->name[1] = 'b'; p->t = 2; return p->name[1] - p->name[0] + p->t; }
