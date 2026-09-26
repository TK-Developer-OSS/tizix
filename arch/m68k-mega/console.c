#include "plat.h"

#define UART_DATA   (*(volatile unsigned char *)UART_DATA_ADDR)
#define UART_STATUS (*(volatile unsigned char *)UART_STATUS_ADDR)

/* 実機は Mega がポーリングでバスを見ているだけ(UART 割込み無し)なので、
 * こちら側もポーリング(STATUS の TXRDY/RXRDY を見るだけ)でよい。 */

void con_putc(char c)
{
    while (!(UART_STATUS & UART_TXRDY)) {}
    UART_DATA = (unsigned char)c;
}

int con_rx_ready(void)
{
    return (UART_STATUS & UART_RXRDY) != 0;
}

unsigned char con_getc(void)
{
    while (!(UART_STATUS & UART_RXRDY)) {}
    return UART_DATA;
}
