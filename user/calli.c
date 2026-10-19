/* user/calli.c - tzcc の組み込みマクロ CALLI / FNADDR の検証用(task.md #70)
 *
 *   FNADDR(name)          関数 name の絶対番地(--tizix-user では +IY 済み)
 *   CALLI(addr, a, b, ..) 絶対番地 addr を a, b, .. を引数に呼ぶ。戻り値は hl
 *
 *   出力を python/test_calli.py が突き合わせる。tzcc 専用(SDCC / m68k では
 *   ビルドしない)。tzcc の掟: 関数間で同名ローカル禁止 / printf 禁止。
 */
#include "stdio.h"

static unsigned tbl[3];

static unsigned f_zero(void)
{
    return 7;
}

static unsigned f_twice(unsigned tw_x)
{
    return tw_x + tw_x;
}

static unsigned f_add3(unsigned a3_a, unsigned a3_b, unsigned a3_c)
{
    return a3_a + a3_b + a3_c;
}

static unsigned f_sq(unsigned sq_x)
{
    return sq_x * sq_x;
}

static void show(char *label, unsigned v)
{
    prs(label);
    prnum(v);
    putchar('\n');
}

int main(int argc, char **argv)
{
    unsigned fp;
    unsigned i;


    fp = FNADDR(f_zero);
    show("calli0 ", CALLI(fp));                       /* 7 */
    fp = FNADDR(f_twice);
    show("calli1 ", CALLI(fp, 21));                   /* 42 */
    show("calli3 ", CALLI(FNADDR(f_add3), 100, 20, 3));   /* 123 */
    /* 引数に CALLI を入れ子: twice(add3(1,2,3)) = 12 */
    show("nest ", CALLI(FNADDR(f_twice), CALLI(FNADDR(f_add3), 1, 2, 3)));

    /* 実行時ディスパッチ表 */
    tbl[0] = FNADDR(f_twice);
    tbl[1] = FNADDR(f_sq);
    tbl[2] = FNADDR(f_zero);
    for (i = 0; i < 3; i++)
        show("tbl ", CALLI(tbl[i], 5));               /* 10, 25, 7 */

    puts("calli: done");
    return 0;
}
