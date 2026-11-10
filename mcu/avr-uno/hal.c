/* hal.c - ATmega328P (Arduino Uno / Nano): console on USART0, 1ms tick on Timer0 (also drives the
 * /dev/timer/0 node), and the registers behind /dev/gpio. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "../common/mcu.h"

const char hal_banner[] MPROGMEM = "atmega328p 16MHz";

/* ---- console: 115200 8N1, interrupt-driven receive, polled transmit ----- */
#define RXN 32                              /* power of two */
static volatile uint8_t rxb[RXN];
static volatile uint8_t rxh, rxt;

ISR(USART_RX_vect)
{
    uint8_t c = UDR0;
    uint8_t n = (uint8_t)((rxh + 1) & (RXN - 1));
    if (n != rxt) {                         /* full: drop */
        rxb[rxh] = c;
        rxh = n;
    }
}

int hal_poll(void)
{
    uint8_t c;
    if (rxh == rxt)
        return -1;
    c = rxb[rxt];
    rxt = (uint8_t)((rxt + 1) & (RXN - 1));
    return c;
}

int hal_getc(void)
{
    int c;
    while ((c = hal_poll()) < 0)
        ;
    return c;
}

void hal_putc(char c)
{
    while (!(UCSR0A & _BV(UDRE0)))
        ;
    UDR0 = (uint8_t)c;
}

/* ---- 1ms tick, and the /dev/timer/0 node on top of it ---------------------- */
static volatile uint32_t ms_count;
static volatile uint16_t t_limit;           /* ms until finish; 0 = stopped */
static volatile uint16_t t_count;
static volatile uint8_t t_finish;           /* set here, cleared by the user */
static volatile uint8_t t_repeat;           /* 0 = one shot (default), 1 = restart */
static volatile uint8_t t_event;            /* an interrupt event is waiting for the script's intr block */

ISR(TIMER0_COMPA_vect)
{
    ms_count++;
    if (t_limit && ++t_count >= t_limit) {
        t_count = 0;
        t_finish = 1;                       /* the interrupt only raises the flags */
        t_event = 1;
        if (!t_repeat)
            t_limit = 0;
    }
}

uint32_t hal_ms(void)
{
    uint32_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        v = ms_count;
    }
    return v;
}

int hal_event_take(void)
{
    uint8_t e;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        e = t_event;
        t_event = 0;
    }
    return e;
}

unsigned hal_timer_get(unsigned which)
{
    return which == TIMER_LIMIT ? t_limit : which == TIMER_FINISH ? t_finish : t_repeat;
}

int hal_timer_set(unsigned which, unsigned v)
{
    if (which == TIMER_LIMIT) {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            t_count = 0;
            t_limit = v;
        }
        return 0;
    }
    if (v > 1)
        return -1;
    if (which == TIMER_FINISH)
        t_finish = (uint8_t)v;
    else
        t_repeat = (uint8_t)v;
    return 0;
}

unsigned hal_free(void)
{
    extern char __heap_start;               /* no malloc: below this is the end of .bss */
    return (unsigned)(SP - (uint16_t)&__heap_start);
}

void hal_init(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = (uint16_t)(F_CPU / (8UL * 115200UL) - 1);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
    UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);

    TCCR0A = _BV(WGM01);                    /* CTC, 16MHz / 64 / 250 = 1kHz */
    OCR0A  = 249;
    TCCR0B = _BV(CS01) | _BV(CS00);
    TIMSK0 = _BV(OCIE0A);

    sei();
}

/* ---- /dev/gpio/N: Arduino numbering. 2-7 = PD2-7, 8-13 = PB0-5, 14-19 (A0-A5) = PC0-5 ---- */
int hal_gpio_valid(unsigned n)
{
    return n >= 2 && n <= 19;               /* 0 and 1 are the uart0 console */
}

static void loc(unsigned n, volatile uint8_t **ddr, volatile uint8_t **port,
                volatile uint8_t **pin, uint8_t *mask)
{
    if (n < 8) {
        *ddr = &DDRD; *port = &PORTD; *pin = &PIND; *mask = _BV(n);
    } else if (n < 14) {
        *ddr = &DDRB; *port = &PORTB; *pin = &PINB; *mask = _BV(n - 8);
    } else {
        *ddr = &DDRC; *port = &PORTC; *pin = &PINC; *mask = _BV(n - 14);
    }
}

int hal_gpio_read(unsigned n)
{
    volatile uint8_t *ddr, *port, *pin;
    uint8_t m;
    loc(n, &ddr, &port, &pin, &m);
    return (*pin & m) != 0;
}

int hal_gpio_write(unsigned n, const char *w)
{
    volatile uint8_t *ddr, *port, *pin;
    uint8_t m;
    loc(n, &ddr, &port, &pin, &m);
    if ((w[0] == '0' || w[0] == '1') && w[1] == 0) {
        if (w[0] == '1')                    /* the level first, then the direction: no glitch */
            *port |= m;
        else
            *port &= (uint8_t)~m;
        *ddr |= m;
    } else if (mstrcmp_P(w, P("in")) == 0) {
        *ddr &= (uint8_t)~m;
        *port &= (uint8_t)~m;
    } else if (mstrcmp_P(w, P("up")) == 0) {
        *ddr &= (uint8_t)~m;
        *port |= m;
    } else {
        return -1;
    }
    return 0;
}
