;===================================================================
; arch/msx/dskio.inc - slot switching and a DSKIO call, shared by the
; boot loader (s2.s) and the kernel trampoline (tramp.s).
;
; Both are assembled at a fixed address in PAGE 2 (0x8000-0xBFFF), and they
; run with interrupts off and the stack in page 2.  Page 2 is never switched, so
; the code and the stack stay visible while pages 0 and 1 swap between RAM
; and the ROMs, and while page 3 is borrowed to reach a sub-slot register.
;
; MSX facts used (from the MSX Technical Data Book; to be checked on openMSX):
;   port 0xA8        primary slot select, 2 bits per page (page 0 in bits 1-0)
;   address 0xFFFF   sub-slot select of the expanded slot that is in page 3
;                    (reads back inverted)
;   0xFCC1 EXPTBL    bit 7 set when primary slot 0 is expanded
;   0xFB22           slot id of the first disk interface (DRVTBL + 1)
;   0xF341 / 0xF342  RAMAD0 / RAMAD1: slot id of the RAM in page 0 / 1
;   disk ROM DSKIO   at 0x4010 in the disk ROM's slot (page 1):
;                    A=drive B=count C=media DE=first sector HL=buffer (page 2/3),
;                    carry set = write; returns carry set on error
; slot id: bit 7 = expanded, bits 3-2 = sub-slot, bits 1-0 = primary slot
;===================================================================

EXPTBL  = 0xFCC1
DRVTBL1 = 0xFB22
RAMAD0  = 0xF341
RAMAD1  = 0xF342
DSKIO   = 0x4010
MEDIA   = 0xF9                  ; 3.5" 2DD, 720KB

; set_page: B = page 0..2, A = slot id.  Clobbers AF BC DE HL.
set_page:
        ld      c, a            ; c = slot id
        ld      a, b
        add     a, a
        ld      b, a            ; b = 2 * page (shift count)
        ld      d, #3           ; d = field mask
        ld      a, c
        and     #3
        ld      e, a            ; e = primary slot
        ld      a, c
        rra
        rra
        and     #3
        ld      h, a            ; h = sub-slot
        ld      a, b
        or      a
        jr      z, 2$
1$:     sla     d
        sla     e
        sla     h
        djnz    1$
2$:     bit     7, c
        jr      z, 4$           ; not expanded: only the primary register
        in      a, (0xA8)
        ld      b, a            ; b = A8 as it is now
        and     #0x3F
        ld      l, a
        ld      a, c
        and     #3
        rrca
        rrca                    ; primary slot << 6
        or      l
        out     (0xA8), a       ; page 3 := the expanded slot, so 0xFFFF is its register
        ld      a, (0xFFFF)
        cpl                     ; the register as it is
        ld      l, a
        ld      a, d
        cpl
        and     l
        or      h               ; this page's field := the sub-slot
        ld      (0xFFFF), a
        ld      a, b
        out     (0xA8), a       ; page 3 back
4$:     in      a, (0xA8)
        ld      l, a
        ld      a, d
        cpl
        and     l
        or      e               ; this page's field := the primary slot
        out     (0xA8), a
        ret

; do_dskio: B = count, DE = first sector, HL = buffer (page 2/3), carry = write.
;   Page 0 becomes the main ROM and page 1 the disk ROM for the call, then both go
;   back to RAM.  Returns carry = error.  Clobbers everything.
do_dskio:
        push    af
        push    bc
        push    de
        push    hl
        ld      b, #0
        ld      a, (EXPTBL)
        and     #0x80           ; main ROM is slot 0 (sub-slot 0 when expanded)
        call    set_page
        ld      b, #1
        ld      a, (DRVTBL1)
        call    set_page
        pop     hl
        pop     de
        pop     bc
        ld      c, #MEDIA
        pop     af              ; the caller's carry
        ld      a, #0           ; drive A (ld does not touch the carry)
        call    DSKIO
        push    af
        ld      b, #1
        ld      a, (RAMAD1)
        call    set_page
        ld      b, #0
        ld      a, (RAMAD0)
        call    set_page
        pop     af
        ret
