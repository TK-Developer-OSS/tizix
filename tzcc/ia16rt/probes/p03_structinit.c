struct pt { int x; int y; };
static struct pt origin = { 1, 2 };
int main(void) { return origin.x + origin.y; }
