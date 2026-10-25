static int s_cnt;
static int bump(int d) { s_cnt = s_cnt + d; return s_cnt; }
int main(void) { bump(2); return bump(3); }
