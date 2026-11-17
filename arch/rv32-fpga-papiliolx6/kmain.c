/* arch/rv32-fpga-papiliolx6 kmain.c - wire the shared src/ kernel (init -> kernel_init -> sh) */
#include "kmem.h"
#include "plat.h"

extern void init(void);			/* src/init.c */
extern void con_putc(char c);
extern void con_init(void);

#define MTIMECMP_LO (*(volatile unsigned long *)(CLINT_MTIMECMP))
#define MTIMECMP_HI (*(volatile unsigned long *)(CLINT_MTIMECMP + 4))
#define MTIME_LO    (*(volatile unsigned long *)(CLINT_MTIME))
#define MTIME_HI    (*(volatile unsigned long *)(CLINT_MTIME + 4))
#define TICK_CLOCKS (MTIME_HZ / TICK_HZ)

/* write the 64-bit mtimecmp safely on RV32 (set the upper half to the maximum first so it never goes through a small value) */
static void set_cmp(unsigned long lo, unsigned long hi)
{
	MTIMECMP_HI = 0xFFFFFFFFUL;
	MTIMECMP_LO = lo;
	MTIMECMP_HI = hi;
}

/* arm the next tick (from the interrupt entry in crt0.S). Adding to the previous deadline = no drift */
void timer_rearm(void)
{
	unsigned long lo = MTIMECMP_LO, hi = MTIMECMP_HI;
	unsigned long nlo = lo + TICK_CLOCKS;

	if (nlo < lo)
		hi++;
	set_cmp(nlo, hi);
}

/* report an exception (from fault in crt0.S. It writes straight to the UART and stops with interrupts closed) */
static void put_hex(unsigned long v)
{
	int i;
	for (i = 28; i >= 0; i -= 4)
		con_putc("0123456789ABCDEF"[(v >> i) & 15]);
}
void exc_report(unsigned long cause, unsigned long epc, unsigned long tval, unsigned long ra, unsigned long sp)
{
	const char *s = "\r\n*** EXC cause=";
	while (*s) con_putc(*s++);
	put_hex(cause);
	s = " epc="; while (*s) con_putc(*s++);
	put_hex(epc);
	s = " tval="; while (*s) con_putc(*s++);
	put_hex(tval);
	s = " ra="; while (*s) con_putc(*s++);
	put_hex(ra);
	s = " sp="; while (*s) con_putc(*s++);
	put_hex(sp);
	s = " HALT ***\r\n"; while (*s) con_putc(*s++);
}

void kmain(void)
{
	unsigned long lo;

	con_init();
	lo = MTIME_LO;

	*(volatile unsigned char *)KW_PIDTAB = PID_IDLE;	/* slot0 = kernel/shell */

	/* timer: arm the first deadline and set MTIE. mstatus.MIE is set by IRQ_ON in kernel_init */
	set_cmp(lo + TICK_CLOCKS, MTIME_HI);
	__asm__ volatile ("csrw mie, %0" :: "r"((1UL << 7) | (1UL << 18)));

	init();

	for (;;)
		__asm__ volatile ("nop");
}
