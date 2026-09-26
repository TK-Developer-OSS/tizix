/* arch/m68k-mega/user/date.c - m68k-mega 専用 date
 *   共有 user/date.c は Unix 秒をカーネルの固定絶対番地 0x8522
 *   (z80/x86-ia16 の kmem.h 固定レイアウト前提)から直接読むが、
 *   m68k-mega の kwork[] はリンク時に決まる可変アドレスのため
 *   そのままでは使えない。syscall #16 (time_get, arch/m68k-mega/
 *   user/tzstdio.h の m68k_get_epoch()) 経由で読む。
 *
 *   共有 user/date.c に #ifdef ARCH_M68K_MEGA で分岐を足す案は
 *   一度試したが、z80pack 側の tzcc(別ビルドツール)が
 *   プリプロセッサ分岐を正しく除去できず、両方の分岐を出力して
 *   「multiple definitions / phase error」でビルドが壊れた
 *   (tzcc は #if/#ifdef の非採用側を捨てられない)。そのため
 *   共有ソースには一切手を入れず、arch 側でこのファイルを丸ごと
 *   差し替える(Makefile の $(TZPORT_DIR)/date.c 個別ルールが
 *   共有 user/date.c より優先してこちらをコピーする)。
 *
 *   read_epoch() 以外(暦計算・出力)は共有版と同一ロジック。
 */
#include "stdio.h"

extern unsigned long m68k_get_epoch(void);

static unsigned long read_epoch(void)
{
    return m68k_get_epoch();
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
