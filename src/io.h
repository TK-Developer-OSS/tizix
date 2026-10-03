#ifndef _IO_H
#define _IO_H

/* tizix カーネル I/O API。物理層(コンソールのポート)は kputchar/kgetchar の
 * 下に封じ込め、実体は arch/<arch>/console.c(約束は console.h)。
 * ユーザーコードは user/stdio.h 経由で標準名
 * (putchar/printf/getchar)から叩く ── 番地はヘッダが隠す。
 *
 * ベクタ(crt0.s, ISR 直後 0x003B〜):
 *   0x003B kexit / 0x003E kputchar / 0x0041 kgetchar /
 *   0x0044 getticks / 0x0047 kprintf
 */

int  kputchar(int c);                  /* 1バイト出力(物理層内包)。0x003E */
int  kgetchar(void);                   /* 1バイト入力(物理層内包)。0x0041。
                                        * リダイレクト中は EOF で -1 */
int  kprintf(const char *fmt, ...);    /* 書式変換。0x0047。%s %d %u %% */
int  con_break(void);   /* 前景中断: Ctrl+C が来ていれば 1。
                         * Ctrl+C 以外の 1 バイトは戻しバッファへ退避する
                         * (捨てない。#33 対話コマンドとの取り合い対策) */
void con_setraw(unsigned char on) __sdcccall(0);
                        /* 呼んだプロセスの間 Ctrl+C を割り込みにしない(tty の raw)。
                         * z80: drv_tbl[48] / m68k: syscall 35。rx(xmodem)が使う */
int  con_pending(void); /* 戻しバッファに 1 バイト持っていれば 1。
                         * DRIVER の kbhit / getc_timeout がこれも見る */

void redir_enable(unsigned char on);   /* 出力先切替(> file) */
void in_enable(unsigned char on);      /* 入力元切替(< file / | 後段) */

#endif
