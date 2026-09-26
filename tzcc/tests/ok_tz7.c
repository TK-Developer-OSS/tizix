// Tizix 実機用: struct スカラーメンバ(char/int/long/ptr)を printf と併用。
//   連結リストを辿り、char/int/long メンバを表示する。
// 期待出力:
//   n0 f=1 v=100 big=4000000000
//   n1 f=2 v=200 big=4000000005
//   sum=300
// RUN: skip Tizix 実機専用 (make tizix TEST_SRC=tests/ok_tz7.c → cpmsim)
#include <stdio.h>

struct Node {
    char flag;
    int val;
    unsigned long big;
    struct Node *next;
};

int main(void) {
    struct Node a;
    struct Node b;
    struct Node *p;
    int sum;

    a.flag = 1; a.val = 100; a.big = 4000000000UL; a.next = &b;
    b.flag = 2; b.val = 200; b.big = 4000000005UL; b.next = 0;

    p = &a;
    printf("n0 f=%d v=%d big=%lu\n", p->flag, p->val, p->big);
    p = p->next;
    printf("n1 f=%d v=%d big=%lu\n", p->flag, p->val, p->big);

    sum = a.val + b.val;
    printf("sum=%d\n", sum);
    return 0;
}
