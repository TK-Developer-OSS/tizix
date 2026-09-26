/* user/history.c - 外部コマンド history(#46)
 *   history : sh の ↑↓ ヒストリ(/root/history、#45)を古い順に番号付きで出す。
 *
 *   ファイル形式は user/sh.c の hist_* と共通:
 *     [0] hhead 次に書くスロット / [1] hcnt 有効件数 / [2 + k*48] スロット k
 *   有効なのは hhead の直前 hcnt 個。100 件溜まるまでは hhead == hcnt なので
 *   [0, hhead) の 1 区間、ローテーションが始まってからは
 *   [hhead, 100) → [0, hhead) の 2 区間になる。
 *
 *   fseek は使わない(掟: 巻き戻し禁止)。先頭から読み進めて区間外を捨て、
 *   2 区間目は fopen し直す。読むのは最大 4.8KB × 2 回。
 *   `history` と打った行もヒストリに積まれてから起動されるので、最後の 1 件は
 *   history 自身になる(bash と同じ)。
 *
 *   tzcc: 仮引数を使わずファイルスコープで渡す / 関数間で同名ローカルを使わない。
 */
#include "stdio.h"

#define HIST_N    100
#define HIST_REC  48

static unsigned char hdr[2];
static char rec[HIST_REC];
static int lo;                  /* dump() が出すスロット [lo, hi) */
static int hi;
static int seq;                 /* 表示番号 1.. */

/* 戻り 0=OK / 1=open 失敗 */
static int dump(void)
{
    FILE *df;
    int k;
    int got;

    df = fopen("/root/history", "r");
    if (df == 0) return 1;
    fread(hdr, 1, 2, df);
    for (k = 0; k < hi; k++) {
        got = fread(rec, 1, HIST_REC, df);
        if (got != HIST_REC) break;
        if (k >= lo) {
            rec[HIST_REC - 1] = 0;
            seq++;
            if (seq < 100) putchar(' ');
            if (seq < 10) putchar(' ');
            prnum(seq);
            prs("  ");
            puts(rec);
        }
    }
    fclose(df);
    return 0;
}

int main(int argc, char **argv)
{
    int head;
    int cnt;

    (void)argc;
    (void)argv;

    /* ヘッダだけ読む(dump が同じ hdr[] を読み直すが中身は同じ) */
    lo = 0;
    hi = 0;
    seq = 0;
    if (dump() != 0) return 0;          /* まだ無い = 空。何も出さない */
    head = hdr[0];
    cnt = hdr[1];
    if (head >= HIST_N || cnt > HIST_N) {
        puts("history: broken file");
        return 1;
    }

    if (cnt <= head) {                  /* 1 区間 */
        lo = head - cnt;
        hi = head;
        dump();
        return 0;
    }
    lo = head + HIST_N - cnt;           /* 2 区間: 古い側 → 新しい側 */
    hi = HIST_N;
    dump();
    lo = 0;
    hi = head;
    dump();
    return 0;
}
