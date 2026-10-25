struct kp { int w; int r; };
struct kp kp1;
int rd(struct kp *q) { return q->w; }
int main(void) { kp1.w = 4; return rd(&kp1); }
