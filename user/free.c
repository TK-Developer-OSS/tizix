/* user/free.c - 外部コマンド free(#64)
 *
 *   free : プロセス枠の使用状況を表示する。全アーキ共通のファイルだが、枠の
 *   数え方がカーネルの作りで違うので、中身は 2 本に分かれている(下の #ifdef)。
 *
 *   z80(4KB ブロック、固定番地の pid 表を直接読む):
 *     blocks 2-7: 6 x 4KB = 24KB
 *     used 3 (12KB)  free 3 (12KB)  largest free run 3 (12KB)
 *     2:# 3:# 4:# 5:. 6:. 7:.
 *
 *   syscall で入るアーキ(stdio.h が TZ_SYSCALL を定義。m68k-mega / esp32-wroom-32e。
 *   スロット 1..N-1、状態は syscall 20 で聞く):
 *     slots 1-30: 30 x 32KB = 960KB
 *     used 2 (64KB)  free 28 (896KB)  largest free run 28 (896KB)
 *     1:# 2:# 3:. …
 *
 *   「largest free run」= 連続した空きの最大。kexec は連続した空きにしか
 *   載せないので、**`sh: X: no free block` が出るかどうかはこの数で決まる**
 *   (合計の空きが足りていても分断されていれば載らない)。
 *   マップの記号: . = 空き / # = プロセス(先頭・継続とも)/ p = パイプの
 *   バッファ / x = 使えない(起動時メモリチェックで不良)。
 *
 *   値の意味は src/kmem.h(PID_FREE=0 / PID_CONT=0xFD / PID_PIPEBUF=0xFC /
 *   PID_BAD=0xFB)と一致させること。
 */
#include "stdio.h"

#ifdef TZ_SYSCALL
/* ------------------------------------------------------------------
 * syscall 版。1 スロットの大きさ(KB)は arch の include/plat.h が
 * PROC_SLOT_KB で出す(コードとデータで枠が分かれるアーキは合計)。
 * ------------------------------------------------------------------ */
#ifndef PROC_SLOT_KB
#error "PROC_SLOT_KB が未定義(arch/<arch>/include/plat.h で 1 スロットの KB 数を定義する)"
#endif
#define PID_PIPEBUF  0xFC

static unsigned long slot_pid(unsigned n)
{
        return syscall5(20, (unsigned long)n, 0, 0, 0);
}

static void kb(unsigned n)
{
        prs(" (");
        prnum(n * PROC_SLOT_KB);
        prs("KB)");
}

int main(int argc, char **argv)
{
        unsigned n, nslot = 0, used = 0, fre = 0, run = 0, best = 0;
        unsigned long p;

        (void)argc;
        (void)argv;
        for (n = 1; ; n++) {
                p = slot_pid(n);
                if (p == 0xFFFFFFFFUL)
                        break;
                nslot++;
                if (p == 0) {
                        fre++;
                        run++;
                        if (run > best) best = run;
                } else {
                        used++;
                        run = 0;
                }
        }

        prs("slots 1-");
        prnum(nslot);
        prs(": ");
        prnum(nslot);
        prs(" x ");
        prnum(PROC_SLOT_KB);
        prs("KB = ");
        prnum(nslot * PROC_SLOT_KB);
        prs("KB\n");
        prs("used ");
        prnum(used);
        kb(used);
        prs("  free ");
        prnum(fre);
        kb(fre);
        prs("  largest free run ");
        prnum(best);
        kb(best);
        putchar('\n');

        for (n = 1; n <= nslot; n++) {
                p = slot_pid(n);
                prnum(n);
                putchar(':');
                if (p == 0) putchar('.');
                else if (p == PID_PIPEBUF) putchar('p');
                else putchar('#');
                putchar(n % 10 == 0 ? '\n' : ' ');
        }
        if (nslot % 10) putchar('\n');
        return 0;
}

#else
/* ------------------------------------------------------------------
 * z80 版。pid 表(KW_PIDTAB = 0x8400、block 0..7 の 8B)を peek で読む。
 * user/sh.c も同じ表を直接読んでいる(前景待ちの判定)。**読むだけ**。
 *
 * 掟(tzcc): printf 不使用 / 乗除算を書かない(×4 は加算)/
 *           関数間で同名ローカル禁止。
 * ------------------------------------------------------------------ */

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

#endif
