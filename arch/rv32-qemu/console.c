/* QEMU virt: the NS16550 UART (UART_BASE in plat.h). Polling only (the UART interrupt is not used) */
#include "plat.h"

#define U8(o)  (*(volatile unsigned char *)(UART_BASE + (o)))
#define LSR_DR   0x01
#define LSR_THRE 0x20

void con_putc(char c)
{
	while (!(U8(5) & LSR_THRE)) {}
	U8(0) = (unsigned char)c;
}

int con_rx_ready(void)
{
	return (U8(5) & LSR_DR) != 0;
}

unsigned char con_getc(void)
{
	while (!(U8(5) & LSR_DR)) {}
	return U8(0);
}
