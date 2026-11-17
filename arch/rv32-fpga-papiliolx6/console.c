/* NEORV32 UART0 (UART_BASE in plat.h). Transmit is polled, receive is interrupt driven (FIRQ2) with a 256-byte
 * into a ring buffer.
 *   Reason: the receive FIFO holds only 16 words, and while the kernel is busy with slow SDRAM work, characters pile up
 *   because pasted characters and the like would be lost (task.md #376).
 *   Even if an interrupt is missed (a race between the clear and the next character), the reader (con_rx_ready / con_getc)
 *   The FIFO is drained first, so no character is lost.
 * Registers: +0 CTRL, +4 BAUD, +8 DATA */
#include "plat.h"

#define R32(o)   (*(volatile unsigned long *)(UART_BASE + (o)))
#define CTRL_RX_EN     (1UL << 0)
#define CTRL_TX_EN     (1UL << 1)
#define CTRL_RX_EMPTY  (1UL << 8)
#define CTRL_TX_FULL   (1UL << 14)
#define CTRL_RX_IRQ_AVAIL (1UL << 16)	/* RX interrupt condition = 1 (one or more words in the FIFO) */

/* assumes the range where BAUD = clock / (2 * baud) can be used as the divider as it is (within 10 bits) */
#define BAUD_DIV (CLK_HZ / (2UL * UART_BAUD))
#if BAUD_DIV >= 1024
#error "UART_BAUD too low for CLK_HZ: prescaler selection is not implemented"
#endif

#define RING 256			/* a power of 2 */
static volatile unsigned char ring[RING];
static volatile unsigned short rhead, rtail;	/* head = where to write, tail = where to read */

void con_init(void)
{
	R32(0) = 0;
	R32(4) = BAUD_DIV;
	R32(0) = CTRL_RX_EN | CTRL_TX_EN | CTRL_RX_IRQ_AVAIL;
}

/* move the UART FIFO into the receive buffer (called from both the interrupt and the reader; the caller closes interrupts) */
static void drain(void)
{
	while (!(R32(0) & CTRL_RX_EMPTY)) {
		unsigned char c = (unsigned char)R32(8);
		unsigned short n = (rhead + 1) & (RING - 1);
		if (n != rtail) {	/* drop the byte when the ring is full */
			ring[rhead] = c;
			rhead = n;
		}
	}
}

/* called from uart_rx_intr in crt0.S. Acknowledge FIRQ2 in mip (write 0) and then drain */
void con_rx_irq(void)
{
	__asm__ volatile ("csrc mip, %0" :: "r"(1UL << 18));
	drain();
}

/* close interrupts and drain. Restore the MIE it had before the call */
static void drain_safe(void)
{
	unsigned long old;
	__asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(old) :: "memory");
	drain();
	if (old & 8)
		__asm__ volatile ("csrsi mstatus, 8" ::: "memory");
}

void con_putc(char c)
{
	while (R32(0) & CTRL_TX_FULL) {}
	R32(8) = (unsigned char)c;
}

int con_rx_ready(void)
{
	drain_safe();
	return rhead != rtail;
}

unsigned char con_getc(void)
{
	unsigned char c;
	while (!con_rx_ready()) {}
	c = ring[rtail];
	rtail = (rtail + 1) & (RING - 1);
	return c;
}
