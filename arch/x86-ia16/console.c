/* tizix x86-ia16 : COM1 16550 UART 物理層 (M1)
 *   QEMU -nographic は COM1(0x3F8)を stdio に橋渡しする。
 *   Z80 の arch/z80pack/console.c と同じ役回り(生 1 バイト I/O)。
 */
#define COM1 0x3F8

/* out/in は AL/DX 固定命令。%b0 だと GCC が AH を割り当てて `outb %ah,%dx`
 * (不正)を吐くことがあるので、レジスタを明示する。 */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %%al, %%dx" : : "a"(val), "d"(port));
}

static inline unsigned char inb(unsigned short port)
{
	unsigned char r;
	__asm__ volatile ("inb %%dx, %%al" : "=a"(r) : "d"(port));
	return r;
}

void con_init(void)
{
	outb(COM1 + 1, 0x00);   /* 割り込み無効                        */
	outb(COM1 + 3, 0x80);   /* DLAB=1                              */
	outb(COM1 + 0, 0x01);   /* 分周比 lo = 1 -> 115200bps          */
	outb(COM1 + 1, 0x00);   /* 分周比 hi = 0                       */
	outb(COM1 + 3, 0x03);   /* 8N1, DLAB=0                         */
	outb(COM1 + 2, 0xC7);   /* FIFO 有効・クリア・14byte 閾値      */
	outb(COM1 + 4, 0x0B);   /* DTR|RTS|OUT2                        */
}

void con_putc(char c)
{
	while (!(inb(COM1 + 5) & 0x20))   /* LSR THRE 待ち */
		;
	outb(COM1, (unsigned char)c);
}

int con_rx_ready(void)
{
	return inb(COM1 + 5) & 0x01;      /* LSR DR */
}

unsigned char con_getc(void)
{
	unsigned char c;
	while (!con_rx_ready())
		;
	c = inb(COM1);
	/* QEMU 脱出ハッチ: Ctrl-](0x1D)だけで QEMU を即終了する。
	 * ★2026-09-12: 以前は Ctrl-C(0x03)もここで横取りしていたが、Ctrl-C は
	 * コマンド中断(sh の con_break() / kgetchar())にも使うキーなので、
	 * 物理層でここに奪われると前景コマンドを止めようとするたびに QEMU
	 * ごと終了してしまっていた(z80pack の cpmsim には Ctrl-\ の脱出キーが
	 * 別にあるのと同じ役回りを Ctrl-] に一本化。Ctrl-C は素通しでゲスト内の
	 * 通常の割込みキーとして使わせる)。 */
	if (c == 0x1D) {
		outb(0xF4, 0);          /* isa-debug-exit (port 0xF4) -> QEMU 終了 */
		for (;;)
			__asm__ volatile ("hlt");
	}
	return c;
}

void con_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			con_putc('\r');
		con_putc(*s++);
	}
}
