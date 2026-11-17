/* tizix's UART loader (it lives in the IMEM ROM; within 4KB).
 *   After power-on, wait for the SDRAM controller to initialise, fill the whole SDRAM (8MB) with 0, print "TZLD" and
 *   Wait for the commands from the host (tools/upload.py).
 *     'W' addr(4) len(4) data(len) xor(1)  ... write to SDRAM (0x80000000 + addr). 'K' on success, 'E' on failure
 *     'G'                                  ... jump to 0x80000000 (the kernel's _start)
 *   len is at most 1024. Numbers are little endian. xor is the exclusive OR of all the bytes of data. */
#ifndef CLK_HZ
#define CLK_HZ 32000000UL
#endif
#ifndef UART_BAUD
#define UART_BAUD 115200UL
#endif
#define UART   0xFFF50000UL
#define R32(a) (*(volatile unsigned long *)(a))
#define RAM    0x80000000UL
#define RAM_BYTES (8UL * 1024 * 1024)

static void putc_(unsigned char c)
{
	while (R32(UART) & (1UL << 14)) {}
	R32(UART + 8) = c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static unsigned char getc_(void)
{
	while (R32(UART) & (1UL << 8)) {}
	return (unsigned char)R32(UART + 8);
}
static unsigned long get32(void)
{
	unsigned long v = getc_();
	v |= (unsigned long)getc_() << 8;
	v |= (unsigned long)getc_() << 16;
	v |= (unsigned long)getc_() << 24;
	return v;
}

void main(void)
{
	volatile unsigned long *p;
	unsigned long i;

	R32(UART) = 0;
	R32(UART + 4) = CLK_HZ / (2UL * UART_BAUD);
	R32(UART) = 3;				/* RX_EN | TX_EN */

	puts_("\r\n0123456789ABCDEF\r\n");	/* UART diagnostic (printed before touching the SDRAM) */

	/* fill the SDRAM with 0 (the first access waits until the controller has finished its initialisation).
	 * the FAT code relies on the free area of the RAM disk being 0, so this is required */
	p = (volatile unsigned long *)RAM;
	for (i = 0; i < RAM_BYTES / 4; i++)
		p[i] = 0;

	puts_("TZLD\r\n");
	for (;;) {
		unsigned char c = getc_();
		if (c == 'W') {
			unsigned long addr = get32(), len = get32(), n;
			volatile unsigned char *d = (volatile unsigned char *)(RAM + addr);
			unsigned char x = 0, b;
			int ok = (len <= 1024) && (addr + len <= RAM_BYTES);
			/* even when out of range, discard len bytes to stay in sync with the host */
			for (n = 0; n < len; n++) {
				b = getc_();
				x ^= b;
				if (ok) d[n] = b;
			}
			if (getc_() != x) ok = 0;
			putc_(ok ? 'K' : 'E');
		} else if (c == 'C') {			/* write the control register (SDRAM capture beat): 'C' v(1) -> 'K' */
			R32(0xF0000000UL) = getc_();
			putc_('K');
		} else if (c == 'R') {			/* read back: 'R' addr(4) len(4) -> len data bytes + xor */
			unsigned long addr = get32(), len = get32(), n;
			volatile unsigned char *d = (volatile unsigned char *)(RAM + addr);
			unsigned char x = 0, b;
			if (addr + len > RAM_BYTES) len = 0;
			for (n = 0; n < len; n++) {
				b = d[n];
				x ^= b;
				putc_(b);
			}
			putc_(x);
		} else if (c == 'G') {
			puts_("GO\r\n");
			while (R32(UART) & (1UL << 15)) {}	/* wait until the transmitter is done */
			__asm__ volatile ("fence.i");
			((void (*)(void))RAM)();
		}
	}
}
