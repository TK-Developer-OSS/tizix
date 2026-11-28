/* console.c - the RC2014 console: SIO/2 channel A, a serial terminal (115200 8N1).
 *   Sending is polled (RR0 bit 2, transmitter empty).  Received bytes come in by interrupt: crt0.s's ISR puts them in the
 *   KW_RXBUF ring (kmem.h, as on z80board), the only reader of the SIO's receiver, and this reads the ring.  The ISR
 *   writes the head, this the tail: no interrupts off needed. */
#include "console.h"
#include "kmem.h"

__sfr __at 0x80 SIOA_C;
__sfr __at 0x81 SIOA_D;

void con_putc(char c)
{
    while (!(SIOA_C & 0x04))
        ;
    SIOA_D = c;
}

int con_rx_ready(void)
{
    return *(volatile unsigned char *)KW_RXHEAD != *(volatile unsigned char *)KW_RXTAIL;
}

/* call only when con_rx_ready() (src/io.c does); unsigned char: no sign extension */
unsigned char con_getc(void)
{
    unsigned char t = *(volatile unsigned char *)KW_RXTAIL;
    unsigned char c = ((volatile unsigned char *)KW_RXBUF)[t];

    *(volatile unsigned char *)KW_RXTAIL = t + 1;
    return c;
}
