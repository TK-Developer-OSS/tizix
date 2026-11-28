;===================================================================
; boot.s - the RC2014 boot loader, one 512B sector at 0xA000.
;
; RomWBW's loader (disk boot) reads the card's sector 2, the boot info (mkdisk.py), and loads the sectors after it to
; the address that sector names: this one, sector 3, to 0xA000, and calls 0xA000 (RomWBW's loader runs at 0x8000 and
; its buffers and stack reach past 0x9000: a load there overwrites it).  RAM is then 64KB: RomWBW's user bank at 0x0000-0x7FFF and its common bank at 0x8000-0xFFFF.  From here on
; RomWBW is not used: interrupts off, read the kernel (KSECT sectors from sector KFIRST) to 0x0100 with the Compact Flash
; card's own registers (8-bit mode, LBA), and jump to 0x0100.
;   CF registers (ports 0x10-0x17): 0x10 data, 0x11 features, 0x12 count, 0x13-0x16 LBA (0x16: 0xE0 | LBA 24-27),
;   0x17 command / status (0x80 busy, 0x08 data request, 0x01 error)
;===================================================================
KFIRST  = 4
KSECT   = 64

        .module boot
        .area   _BOOT (ABS)
        .org    0xA000
        di
        ld      sp, #0xA400
        call    wait
        ld      a, #0x01                ; 8-bit transfers (RomWBW did it already; a reset card has it off)
        out     (0x11), a
        ld      a, #0xEF                ; set features
        out     (0x17), a
        call    wait
        ld      hl, #0x0100
        ld      de, #KFIRST
        ld      a, #KSECT
1$:     push    af
        call    wait
        ld      a, #1
        out     (0x12), a
        ld      a, e
        out     (0x13), a
        ld      a, d
        out     (0x14), a
        xor     a, a
        out     (0x15), a
        ld      a, #0xE0
        out     (0x16), a
        ld      a, #0x20                ; read sectors
        out     (0x17), a
2$:     in      a, (0x17)
        and     a, #0x89
        cp      a, #0x08
        jr      z, 3$
        and     a, #0x81
        cp      a, #0x01                ; error, not busy: give up (RomWBW's own message stays on the screen)
        jr      z, fail
        jr      2$
3$:     ld      bc, #0x0010             ; b = 0: 256 bytes, twice
        inir
        inir
        inc     de
        pop     af
        dec     a
        jr      nz, 1$
        jp      0x0100

wait:   in      a, (0x17)
        and     a, #0x80
        jr      nz, wait
        ret

fail:   ld      hl, #msg
4$:     in      a, (0x80)               ; SIO/2 channel A: wait for the transmitter
        and     a, #0x04
        jr      z, 4$
        ld      a, (hl)
        or      a, a
        jr      z, .
        out     (0x81), a
        inc     hl
        jr      4$
msg:    .ascii  "\r\ntizix boot: CF read error\r\n"
        .db     0
