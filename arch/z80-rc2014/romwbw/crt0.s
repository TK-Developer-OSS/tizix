.module crt0

;===================================================================
; arch/z80-rc2014 crt0.s (RC2014 with RomWBW).  boot.s reads the kernel image to 0x0100 and jumps to it.
;
;   RC2014 specific here:
;     - RAM is the 64KB RomWBW left mapped (512K RAM / 512K flash module, ports 0x78-0x7C untouched); RomWBW is not
;       called again, so its code at the top of RAM is overwritten like the rest
;     - the SIO/2 (ports 0x80 A control, 0x81 A data, 0x82 B control, 0x83 B data), interrupt mode 1:
;         channel A = the console (115200 8N1 with the usual 7.3728MHz clock); a received byte interrupts and goes into
;         the receive ring (KW_RXBUF, as on z80board; console.c reads it)
;         channel B = the timer: it sends bytes to nothing, and its "transmitter empty" interrupt comes once a byte,
;         11 bits (8 data, 2 stop) at its clock / 64.  The RC2014 has no timer of its own (no CTC on a Zed / Zed Pro),
;         so the Dual Clock module's CLOCK2 (channel B's clock) is set to 0.3072MHz: 436 interrupts/s, every 7th is a
;         tick (62Hz, TICK_HZ in the Makefile)
;   The rest (vectors at 0x0038.., scheduler, init) is shared with the other z80 ports.
;===================================================================

        .include "../../common-z80/crt0-defs.inc"

KW_RXHEAD = 0x8D96              ; the receive ring (kmem.h KW_RX*): head (written here), tail (console.c)
KW_RXTAIL = 0x8D97
KW_RXBUF  = 0x9F00              ; 256B on a page boundary: the index wraps by itself
TICKDIV   = 7

		.area   _HEADER (ABS)
        .org    0x0100
        jp      start

        .area   _CODE
start:
        di
        ld      sp, #0x9000
        im      1
        ; --- the fixed vectors 0x0038-0x006C, copied from a table (vec_table below) ---
        ;     0x0038 isr        0x003B _kexit      0x003E _kputchar   0x0041 _kgetchar
        ;     0x0044 _getticks  0x0047 _kprintf    0x004A _time_get   0x004D _time_set
        ;     0x0050 jp (hl)    (SDCC 4.x helper)
        ;     0x0052 __divuint  0x0055 __moduint   0x0058 __divsint   0x005B __modsint
        ;     0x005E __divulong 0x0061 __mullong   0x0064 __mulint
        ;     0x0067 _memcmp    0x006A _strcmp
        ;   The addresses are fixed: external commands link against them (-g _kputchar=0x003E ...).
        ld      hl, #vec_table
        ld      de, #0x0038
        ld      bc, #0x0035
        ldir

        .include "../../common-z80/crt0-init.inc"

        ; --- SIO/2 (interrupts stay off until _init turns them on) ---
        ld      hl, #sio_init
        ld      b, #sio_init_end - sio_init
1$:     ld      c, (hl)                 ; port
        inc     hl
        ld      a, (hl)                 ; value
        inc     hl
        out     (c), a
        dec     b
        djnz    1$
        xor     a, a
        ld      (KW_RXHEAD), a
        ld      (KW_RXTAIL), a
        ld      a, #TICKDIV
        ld      (tickdiv), a
        out     (0x83), a               ; the first byte for channel B: from now on it interrupts once a byte

        call    _init
        halt

; (port, value) pairs
sio_init:
        .db     0x80, 0x18              ; A: channel reset
        .db     0x80, 0x04, 0x80, 0xC4  ; A: WR4 = clock / 64, 1 stop bit, no parity
        .db     0x80, 0x03, 0x80, 0xC1  ; A: WR3 = receive 8 bits, receiver on
        .db     0x80, 0x05, 0x80, 0xEA  ; A: WR5 = DTR, transmit 8 bits, transmitter on, RTS
        .db     0x80, 0x01, 0x80, 0x18  ; A: WR1 = interrupt on every received byte
        .db     0x82, 0x18              ; B: channel reset
        .db     0x82, 0x04, 0x82, 0xCC  ; B: WR4 = clock / 64, 2 stop bits, no parity
        .db     0x82, 0x03, 0x82, 0xC0  ; B: WR3 = receiver off
        .db     0x82, 0x05, 0x82, 0x68  ; B: WR5 = transmit 8 bits, transmitter on
        .db     0x82, 0x01, 0x82, 0x02  ; B: WR1 = interrupt when the transmitter is empty
sio_init_end:

vec_table:
        jp      isr
        jp      _kexit
        jp      _kputchar
        jp      _kgetchar
        jp      _getticks
        jp      _kprintf
        jp      _time_get
        jp      _time_set
        .db     0xE9, 0xC9              ; 0x0050: jp (hl) ; ret
        jp      __divuint
        jp      __moduint
        jp      __divsint
        jp      __modsint
        jp      __divulong
        jp      __mullong
        jp      __mulint
        jp      _memcmp
        jp      _strcmp

; ---- 0x0038 ISR: received bytes into the ring; channel B's byte: every TICKDIV-th is a tick (save/pick/restore) ----
isr:
        push    af
        in      a, (0x80)               ; A: RR0, bit 0 = a byte received
        rrca
        jr      nc, 2$
        push    hl
        ld      h, #>KW_RXBUF
1$:     ld      a, (KW_RXHEAD)
        ld      l, a
        inc     a
        ld      (KW_RXHEAD), a          ; no full check: an overflow overwrites the oldest (as on z80board)
        in      a, (0x81)
        ld      (hl), a
        in      a, (0x80)
        rrca
        jr      c, 1$
        ld      a, #0x30                ; A: error reset (an overrun)
        out     (0x80), a
        pop     hl
2$:     in      a, (0x82)               ; B: RR0, bit 2 = transmitter empty
        and     a, #0x04
        jr      z, 3$
        out     (0x83), a               ; the next byte (clears the interrupt)
        ld      a, (tickdiv)
        dec     a
        jr      z, 4$
        ld      (tickdiv), a
3$:     pop     af
        ei
        reti
4$:     ld      a, #TICKDIV
        ld      (tickdiv), a
        .include "../../common-z80/crt0-sched.inc"

        .include "../../common-z80/crt0-areas.inc"

        .area   _DATA
tickdiv:
        .ds     1
