int main(void) { int a; a = 0x0F; return ((a << 2) | 1) ^ (~a & 0x30); }
