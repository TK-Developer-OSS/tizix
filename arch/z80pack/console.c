#include "con.h"

/* ------------------------------------------------------------------
 * 層1 実体: cpmsim console
 *   port 0 = status (bit0 相当: 0 なら未着)
 *   port 1 = data
 * 実機(Z84C000 + FT245RL)でもポート番号を同じ 0/1 に合わせてあるので、
 * 当面 #ifdef は撒かない。実機分岐が要る段(初期化やステータス極性が
 * 違うと判明した段)でこのファイルだけを差し替える。
 * ------------------------------------------------------------------ */
#define CON_STAT 0x00
#define CON_DATA 0x01

__sfr __at CON_STAT CONSTAT;
__sfr __at CON_DATA CONDATA;

void con_putc(char c)
{
    CONDATA = c;
}

/* 返り値は unsigned char。int へ符号拡張されないことが必須:
 * fatcmd.c の `while ((c = getchar()) >= 0)` は 0x80-0xFF を
 * 負値と誤解すると途中で切れる。 */
unsigned char con_getc(void)
{
    while (CONSTAT == 0)
        ;
    return CONDATA;
}
