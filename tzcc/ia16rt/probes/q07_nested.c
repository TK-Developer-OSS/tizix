struct in { int c; }; struct out { int a; struct in b; };
struct out o;
int main(void) { o.a = 3; o.b.c = 4; return o.a * 10 + o.b.c; }
