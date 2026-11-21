.module crt0

;===================================================================
;
;===================================================================

        .include "../common-z80/crt0-defs.inc"

KW_RXHEAD     = 0x8D96
KW_RXTAIL     = 0x8D97
KW_RXBUF      = 0x9F00
KW_RXBUF_SIZE = 256

; ======================================================================
; ======================================================================
        .area   _HEADER (ABS)

        .org    0x0000
        jp      start

        .org    0x0038
        jp      isr

        .org    0x003B
        jp      _kexit
        .org    0x003E
        jp      _kputchar
        .org    0x0041
        jp      _kgetchar
        .org    0x0044
        jp      _getticks
        .org    0x0047
        jp      _kprintf
        .org    0x004A
        jp      _time_get
        .org    0x004D
        jp      _time_set

        ; --- SDCC 4.x helper (0x0050: jp (hl)) ---
        .org    0x0050
        jp      (hl)
        ret

        .org    0x0052
        jp      __divuint
        .org    0x0055
        jp      __moduint
        .org    0x0058
        jp      __divsint
        .org    0x005B
        jp      __modsint
        .org    0x005E
        jp      __divulong
        .org    0x0061
        jp      __mullong
        .org    0x0064
        jp      __mulint
        .org    0x0067
        jp      _memcmp
        .org    0x006A
        jp      _strcmp

        .org    0x006D
        jp      kyield_entry

        .area   _CODE
start:
        di
        ld      sp, #0x9000
        ; TI-84+ memory map: 0x0000-0x3FFF = flash page 0 (fixed), 0x4000-0x7FFF = bank A (flash page 1),
        ; 0x8000-0xBFFF = bank B (RAM page 1), 0xC000-0xFFFF = RAM page 0 (port 5).
        ; => the same 32KB ROM + 32KB RAM layout as arch/z80board.
        xor     a
        out     (0x04), a       ; memory mode 0 + first timer speed 0 (about 512Hz)
        out     (0x05), a       ; RAM page 0 at 0xC000
        ld      a, #1
        out     (0x06), a       ; flash page 1 at 0x4000
        ld      a, #0x81
        out     (0x07), a       ; RAM page 1 at 0x8000 (bit 7 = RAM)
        ; link assist: RX interrupt only (the console). TX is polled.
        ld      a, #1
        out     (0x08), a
        ; first timer interrupt (the scheduler tick)
        ld      a, #0x02
        out     (0x03), a
        jr      crt0_bringup_done


;--------------------------------------------------------
; _ti_xfer(unsigned char *buf, unsigned bank, unsigned off, unsigned dir)  (sdcccall 0, all args on the stack)
;   Copy 512 bytes between buf and a page of the calculator's memory that is mapped onto bank A (0x4000):
;   bank = the value for port 6 (a flash page 0..63, or 0x80|n for RAM page n), off = offset in the page.
;   dir 0 = page -> buf, otherwise buf -> page (RAM pages only). Bank A holds the kernel's upper half, so
;   this routine MUST live in page 0 (below 0x4000: crt0 is linked first) and runs with interrupts off.
;   The interrupt state is restored on exit.
;--------------------------------------------------------
        .globl  _ti_xfer
        .globl  _lcd_init
_ti_xfer:
        push    ix
        ld      ix, #0
        add     ix, sp
        ld      a, i            ; P/V = IFF2
        push    af
        di
        ld      a, 6(ix)        ; bank
        out     (0x06), a
        ld      l, 8(ix)        ; off
        ld      h, 9(ix)
        ld      bc, #0x4000
        add     hl, bc          ; HL = 0x4000 + off
        ld      e, 4(ix)        ; DE = buf
        ld      d, 5(ix)
        ld      a, 10(ix)       ; dir
        or      a, 11(ix)
        ld      bc, #512
        jr      z, 2$
        ex      de, hl          ; write: buf -> page
2$:     ldir
        ld      a, #1
        out     (0x06), a       ; back to the kernel's flash page 1
        pop     af
        jp      po, 1$
        ei
1$:     pop     ix
        ret

crt0_bringup_done:

        .include "../common-z80/crt0-init.inc"

        xor     a
        ld      (KW_RXHEAD), a
        ld      (KW_RXTAIL), a

        im      1

        call    _lcd_init
        call    _init
        halt

;
isr:
        push    af
        in      a, (0x09)       ; link assist status
        bit     4, a            ; rx_ready
        jr      z, isr_timer    ; no byte => first timer
        ; --- link assist RX: drain one byte into the ring (AF and HL only) ---
        push    hl
        ld      a, (KW_RXHEAD)
        ld      l, a
        inc     a
        ld      (KW_RXHEAD), a
        ld      h, #>(KW_RXBUF)
        in      a, (0x0A)       ; received byte (clears rx_ready)
        ld      (hl), a
        pop     hl
        pop     af
        ei
        reti

;
;
kyield_entry:
        di
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        jr      isr_save

isr_timer:
        xor     a               ; acknowledge: mask off and on again
        out     (0x03), a
        ld      a, #0x02
        out     (0x03), a
        .include "../common-z80/crt0-sched.inc"

        .include "../common-z80/crt0-areas.inc"
