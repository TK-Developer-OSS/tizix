/* user/date.c - 外部コマンド date
 *   Unix 秒は カーネルの絶対番地 0x8522 (KW_EPOCH_SEC) にある。
 *   time_get() ベクタ経由だと 32bit 戻り値の受け渡し規約差で上位/下位ワードが
 *   入れ替わる事象があったため、ここでは直接読む(メモリは flat)。
 *   32bit read は非アトミックなので、2回読んで一致するまで繰り返す
 *   (di/ei をユーザー空間で使わずに済ませる)。
 *
 *   #31 コストモデル対応(2590 -> ):
 *     ・32bit を使う範囲を「epoch 秒 → 日数 + 秒余り」の 1 段だけに限定した。
 *       days / ydays を unsigned long から unsigned へ落とす(65535 日 = 西暦
 *       2149 年まで表せるので実用上十分)。tzcc の 32bit 演算は 1 箇所ごとに
 *       大きく展開されるので、これだけで効く。
 *     ・print_pad(pow10[] テーブル + 桁ごとの減算ループ)を廃止し、2 桁固定の
 *       p2() 1 本にした。年は 100 で割った商と余りを p2 で 2 回出す。
 *       除算・乗算は使わない(減算ループのみ)。
 */
#include "stdio.h"

#define EPOCH_ADDR  ((volatile unsigned long *)0x8522)

static unsigned long read_epoch(void)
{
    unsigned long a;
    unsigned long b;
    for (;;) {
        a = *EPOCH_ADDR;
        b = *EPOCH_ADDR;
        if (a == b) return a;
    }
}

static unsigned char days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* 2 桁ゼロパディング出力(0..99)。除算は使わず減算ループのみ。 */
static void p2(unsigned n)
{
    unsigned p2d;
    p2d = 0;
    for (;;) {
        if (n < 10u) break;
        n -= 10u;
        p2d++;
    }
    putchar((char)('0' + p2d));
    putchar((char)('0' + n));
}

void main(void)
{
    unsigned long t;
    unsigned days;
    unsigned rem_sec;
    unsigned y, m, d;
    unsigned hh, mm, ss;
    unsigned ydays;
    unsigned dim;
    unsigned yh;
    unsigned char leap;

    t = read_epoch();

    /* 32bit を使うのはここだけ。以降は 16bit で足りる。 */
    days = 0;
    for (;;) {
        if (t < 86400UL) break;
        t -= 86400UL;
        days++;
    }
    rem_sec = (unsigned)t;

    hh = 0;
    while (rem_sec >= 3600u) { rem_sec -= 3600u; hh++; }
    mm = 0;
    while (rem_sec >= 60u) { rem_sec -= 60u; mm++; }
    ss = rem_sec;

    y = 1970;
    for (;;) {
        leap = ((y & 3u) == 0);
        ydays = leap ? 366u : 365u;
        if (days < ydays) break;
        days -= ydays;
        y++;
    }

    m = 1;
    leap = ((y & 3u) == 0);
    while (m <= 12u) {
        dim = days_in_month[m - 1];
        if (m == 2u && leap) dim = 29u;
        if (days < dim) break;
        days -= dim;
        m++;
    }
    d = days + 1u;

    yh = 0;
    while (y >= 100u) { y -= 100u; yh++; }
    p2(yh);
    p2(y);
    putchar('-');
    p2(m);
    putchar('-');
    p2(d);
    putchar(' ');
    p2(hh);
    putchar(':');
    p2(mm);
    putchar(':');
    p2(ss);
    putchar('\n');
}
