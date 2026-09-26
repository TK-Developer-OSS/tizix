/* user/free.c - 外部コマンド free(#64)
 *
 *   free : プロセス枠(4KB ブロック)の使用状況を表示する。
 *
 *     blocks 2-7: 6 x 4KB = 24KB
 *     used 3 (12KB)  free 3 (12KB)  largest free run 3 (12KB)
 *     2:# 3:# 4:# 5:. 6:. 7:.
 *
 *   「largest free run」= 連続した空きの最大。kexec は連続した空きにしか
 *   載せないので、**`sh: X: no free block` が出るかどうかはこの数で決まる**
 *   (合計の空きが足りていても分断されていれば載らない)。
 *   マップの記号: . = 空き / # = プロセス(先頭・継続とも)/ p = パイプの
 *   バッファ / x = 使えない(起動時メモリチェックで不良)。
 *
 *   pid 表(KW_PIDTAB = 0x8400、block 0..7 の 8B)を peek で読む。
 *   user/sh.c も同じ表を直接読んでいる(前景待ちの判定)。**読むだけ**。
 *   値の意味は src/kmem.h(PID_FREE=0 / PID_CONT=0xFD / PID_PIPEBUF=0xFC /
 *   PID_BAD=0xFB)と一致させること。
 *
 *   掟(tzcc): printf 不使用 / 乗除算を書かない(×4 は加算)/
 *             関数間で同名ローカル禁止。
 */
#include "stdio.h"

#define KW_PIDTAB    0x8400
#define BLK_LO       2
#define BLK_HI       8
#define PID_PIPEBUF  0xFC
#define PID_BAD      0xFB

static void kb(unsigned char kb_n)
{
    unsigned kb_v;

    kb_v = kb_n + kb_n;
    kb_v = kb_v + kb_v;               /* ×4 = KB */
    prs(" (");
    prnum(kb_v);
    prs("KB)");
}

int main(int argc, char **argv)
{
    unsigned char fr_b;
    unsigned char fr_p;
    unsigned char fr_used;
    unsigned char fr_free;
    unsigned char fr_run;
    unsigned char fr_best;

    (void)argc;
    (void)argv;
    fr_used = 0;
    fr_free = 0;
    fr_run = 0;
    fr_best = 0;
    for (fr_b = BLK_LO; fr_b < BLK_HI; fr_b++) {
        fr_p = peek(KW_PIDTAB + fr_b);
        if (fr_p == 0) {
            fr_free++;
            fr_run++;
            if (fr_run > fr_best) fr_best = fr_run;
        } else {
            fr_used++;
            fr_run = 0;
        }
    }

    prs("blocks 2-7: 6 x 4KB = 24KB\n");
    prs("used ");
    prnum(fr_used);
    kb(fr_used);
    prs("  free ");
    prnum(fr_free);
    kb(fr_free);
    prs("  largest free run ");
    prnum(fr_best);
    kb(fr_best);
    putchar('\n');

    for (fr_b = BLK_LO; fr_b < BLK_HI; fr_b++) {
        fr_p = peek(KW_PIDTAB + fr_b);
        putchar('0' + fr_b);
        putchar(':');
        if (fr_p == 0) putchar('.');
        else if (fr_p == PID_PIPEBUF) putchar('p');
        else if (fr_p == PID_BAD) putchar('x');
        else putchar('#');
        putchar(' ');
    }
    putchar('\n');
    return 0;
}
