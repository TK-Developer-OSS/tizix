#include "include/hw.h"
#include "../../../src/io.h"

__sfr __at CON_PORT CONDATA;

/* 実機は SYS_STAT_PORT 経由の TXE#/RXF# ハンドシェイクを持たない
 * (A0-3 が未デコードで port 0x00 も当てにできない上、そもそも
 * bios.s の実機動作実績が無条件 OUT/IN のみで確認済み)。
 * ステータス待ちは行わず、bios.s / z80pack 版と同じ素の OUT/IN にする。
 * 受信側は現状ハンドシェイク無しなので、実データが来る前に読むと
 * ゴミを返し得る(既知の制限。まずは送信を通すことを優先)。 */
void con_putc(char c)
{
    CONDATA = c;
}

/* 返り値は unsigned char。int へ符号拡張されないことが必須:
 * fatcmd.c の `while ((c = getchar()) >= 0)` は 0x80-0xFF を
 * 負値と誤解すると途中で切れる(z80pack/console.c と同じ注意)。 */
unsigned char con_getc(void)
{
    return CONDATA;
}
