#include "plat.h"

#define REG(a) (*(volatile unsigned long *)(a))

/* UART0 をポーリングで叩く(UART の割込みは使わない)。ROM の uart_tx_one_char 等は
 * 使わない: ROM 関数は windowed ABI 前提で、tizix 側の ABI の選択を縛るため。
 *
 * 受信は、ハードの FIFO(128B)の手前にソフトのリング(rxq)を置く。
 *   flash の書き込みや消去で割込みを止めている間も相手は送ってくるので、FIFO が
 *   あふれてバイトを落とす(実機の rx で、SOH を受けた直後に前のブロックを flash へ
 *   書く間に残り 132B が流れ込み、毎回 NAK になった。QEMU では flash が一瞬で
 *   終わるので出ない)。diskio.c が flash の処理の切れ目ごとに、kmain.c のタイマが
 *   tick ごとに con_poll_rx() を呼んで FIFO をここへ移す。
 *   取り出しは必ずリングが先(リングの中身のほうが FIFO の中身より古い)。 */
#define RXQ_SIZE 1024                     /* 2 の冪 */
static unsigned char rxq[RXQ_SIZE];
static volatile unsigned rxq_head, rxq_tail;

void con_poll_rx(void)
{
    while ((REG(UART0_STATUS) & 0xFF) != 0) {
        unsigned nh = (rxq_head + 1) & (RXQ_SIZE - 1);
        if (nh == rxq_tail)
            return;                       /* リングも満杯なら FIFO に残しておく */
        rxq[rxq_head] = (unsigned char)REG(UART0_FIFO);
        rxq_head = nh;
    }
}

/* 受信の割込み(#109)。tick ごと・flash の切れ目ごとの吸い上げだけでは、WiFi が割込みを
 * 止めている間に矢印キーの連打(↑×100 = 300B)が FIFO をあふれた(実機の test_sh_hist)。
 * FIFO に 64B たまるか、少し途切れたら割込みを上げて、すぐリングへ移す。 */
#define UART0_INT_ENA    0x3FF4000CUL
#define UART0_INT_CLR    0x3FF40010UL
#define UART0_CONF1      0x3FF40024UL
#define UART_RX_INTS     ((1UL << 0) | (1UL << 8))   /* RXFIFO_FULL / RXFIFO_TOUT */
#define UART_INTNO       9                          /* CPU の割込み 9 番(レベル 1) */
#define ETS_UART0_INTR_SOURCE 34
extern void intr_matrix_set(int cpu_no, unsigned long source, unsigned long intr_num);   /* ROM */
extern void esp32_set_isr(int n, void (*f)(void *), void *arg);                          /* kmain.c */
extern void esp32_ints_on(unsigned long mask);

static void uart_isr(void *arg)
{
    (void)arg;
    con_poll_rx();
    if ((REG(UART0_STATUS) & 0xFF) != 0)
        REG(UART0_INT_ENA) = 0;           /* リングが満杯: 取り出されるまで割込みを止める(鳴りっぱなし防止) */
    REG(UART0_INT_CLR) = UART_RX_INTS;
}

void con_init(void)
{
    unsigned long c1 = REG(UART0_CONF1);
    c1 &= ~(0x7FUL | (0x7FUL << 24));
    c1 |= 64UL | (10UL << 24) | (1UL << 31);  /* 64B で FULL、10 文字分途切れたら TOUT */
    REG(UART0_CONF1) = c1;
    REG(UART0_INT_CLR) = 0xFFFFFFFFUL;
    REG(UART0_INT_ENA) = UART_RX_INTS;
    intr_matrix_set(0, ETS_UART0_INTR_SOURCE, UART_INTNO);
    esp32_set_isr(UART_INTNO, uart_isr, 0);
    esp32_ints_on(1UL << UART_INTNO);
}

void con_putc(char c)
{
    while (((REG(UART0_STATUS) >> 16) & 0xFF) >= UART_FIFO_LEN - 2) {}
    REG(UART0_FIFO) = (unsigned char)c;
}

int con_rx_ready(void)
{
    return rxq_head != rxq_tail || (REG(UART0_STATUS) & 0xFF) != 0;
}

unsigned char con_getc(void)
{
    unsigned char c;

    if (rxq_head != rxq_tail) {
        c = rxq[rxq_tail];
        rxq_tail = (rxq_tail + 1) & (RXQ_SIZE - 1);
        REG(UART0_INT_ENA) = UART_RX_INTS;  /* 満杯で止めていたら戻す */
        return c;
    }
    while ((REG(UART0_STATUS) & 0xFF) == 0) {}
    return (unsigned char)REG(UART0_FIFO);
}
