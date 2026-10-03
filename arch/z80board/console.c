#include "console.h"
#include "kmem.h"       /* KW_RXHEAD / KW_RXTAIL / KW_RXBUF(crt0.s の ISR が埋めるリング) */
#include "include/hw.h"

/* ------------------------------------------------------------------
 * 層1 実体: z80board(Z84C000 + FT245RL)のコンソール物理層
 *
 * 2026-09-30: リンクに入れた(Makefile の KOBJ_ARCH)。それまでこのファイルは
 * 一度も繋がれておらず、同じ役目のコードが src/io.c の ARCH_Z80BOARD の枝に
 * 直書きされていた。中身はその枝をそのまま移したもの。src/io.c の
 * kputchar / kgetchar / con_break がここを呼ぶ。
 * ------------------------------------------------------------------ */

__sfr __at CON_PORT CONDATA;

/* 送信: 実機は SYS_STAT_PORT 経由の TXE# ハンドシェイクを見ない(bios.s の
 * 実機動作実績が無条件 OUT で確認済み)。ISR とは競合しないので直接叩く。 */
void con_putc(char c)
{
    CONDATA = c;
}

/* 受信(#59 実タイマ割り込み化): FT245 の受信ポート(0x01)とステータス
 * ラッチ(0x10, bit6=~RXF)は crt0.s の ISR が排他的に触る。フォアグラウンド
 * 側が同じポートを直接ポーリングすると ISR の読み出しと競合してバイトを
 * 取りこぼす/二重取得するため、ここは ISR が埋める KW_RXBUF リング
 * (kmem.h)だけを見る。 */
int con_rx_ready(void)
{
    return *(volatile unsigned char *)KW_RXHEAD !=
           *(volatile unsigned char *)KW_RXTAIL;
}

/* リングから 1 バイト取る。con_rx_ready() が真のときに呼ぶこと(src/io.c は
 * 必ず先に確かめる。空のまま呼ぶと古いバイトを返す ── 割り込み禁止の中から
 * 呼ばれるので、ここで待つと二度と戻らない)。
 * ISR(単一のライタ)とはヘッド/テールが別変数なので di/ei 無しで安全な
 * SPSC リング(#33 con_ung と同じ発想)。KW_RXBUF_SIZE は 2 の冪(#87 で 256)
 * 前提で and によりラップする(crt0.s の isr と同じ前提)。
 * 返り値は unsigned char。int へ符号拡張されないことが必須。 */
unsigned char con_getc(void)
{
    unsigned char t = *(volatile unsigned char *)KW_RXTAIL;
    unsigned char c = ((volatile unsigned char *)KW_RXBUF)[t];

    *(volatile unsigned char *)KW_RXTAIL = (t + 1) & (KW_RXBUF_SIZE - 1);
    return c;
}
