struct vn { char t; int v; };
char buf[32];
int main(void) {
    struct vn *p;
    p = (struct vn *)buf;
    p->v = 9;
    return p->v;
}
