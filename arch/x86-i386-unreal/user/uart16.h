/* arch/x86-i386-unreal/user/uart16.h -- ネットの下回り(netmodem16.c / netcli16.c)がシリアルを使う口。
 *   ポートは直接叩かず、カーネルの割り込み受信ドライバ(uart.s、syscall 56〜60)を通す。受信はカーネルがリングに
 *   貯めておくので、待つ間は u_wait で寝てよい(データが来るか時間切れで戻る。その間ほかのプロセスが走り、全員が待ちなら
 *   CPU は hlt で休む)。以前のポーリングは、待つと 16 バイトの FIFO が溢れ、待たないと CPU を使い切っていた。
 *   使う側で NET_UART(0x3F8 = COM1 のみ / 0x2F8 = COM2、無ければ COM1)と NET_BAUD_DIV を決めてから読み込む。 */
#ifndef UART16_H
#define UART16_H

/* 使うポート(0 = COM1 / 1 = COM2)。NET_UART を 0x3F8 にしたときは COM1 だけ、既定(0x2F8)なら COM2 を試して、無ければ COM1。
 * COM が 1 つの機械(T105)では、`serialcon off` でコンソールから COM1 を外してあれば、COM1 がモデムになる
 * (外していなければカーネルが断る)。 */
static unsigned u_port = 0xFF;          /* 0xFF = まだ開いていない(カーネルは 2 以上を無視する) */

/* 115200 / NET_BAUD_DIV bps、8N1、DTR/RTS を立てて開く(溜まっていた受信は捨てる)。0 = 開いた / -1 = 使えるポートが無い */
static int u_open(void)
{
    unsigned p;
#if (NET_UART) == 0x3F8
    p = 0;
#else
    p = 1;
    if (_syscall5_asm(56, 1, NET_BAUD_DIV, 3, 0, 0) == 0) { u_port = 1; return 0; }
    p = 0;
#endif
    if (_syscall5_asm(56, p, NET_BAUD_DIV, 3, 0, 0) == 0) { u_port = p; return 0; }
    return -1;
}
static void u_write(const void *b, unsigned n) { _syscall5_asm(58, 0, u_port, n, 0, (unsigned)b); }
/* 溜まっている分を max まで(待たない) */
static unsigned u_read(void *b, unsigned max) { return _syscall5_asm(57, 0, u_port, max, 0, (unsigned)b); }
/* データが来るか ticks tick(1/100 秒)経つまで寝る。戻り = 溜まっているバイト数(0 = 時間切れ) */
static unsigned u_wait(unsigned ticks) { return _syscall5_asm(59, u_port, ticks, 0, 0, 0); }
/* モデム状態レジスタ(bit7 = DCD) */
static unsigned u_msr(void) { return _syscall5_asm(60, u_port, 0, 0, 0, 0); }

#endif
