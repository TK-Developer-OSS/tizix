int main(void) { int a; int *p; int **pp; a = 5; p = &a; pp = &p; return **pp; }
