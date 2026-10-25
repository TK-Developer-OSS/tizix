extern int g_ext;
int g_own;
int f(void) { g_own = 3; return g_ext + g_own; }
