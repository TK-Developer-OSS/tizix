;===================================================================
; hi.s - the screen and disk code of the Amstrad CPC 6128 port, assembled for 0xF000 (loaded there by boot.s, never
; touched by the kernel).
;
; The kernel runs in RAM configuration 1 (banks 0,1,2,7; both ROMs off).  The screen is bank 3 (the CRTC always shows
; 0xC000-0xFFFF of the base 64KB), which configuration 3 (banks 0,3,2,7) puts at 0x4000.  Bank 7 at 0xC000 is the same
; in both, so everything that runs while the screen is paged in lives up here: interrupts off, a private stack, switch,
; draw, switch back.  Mode 2 (640x200, 1 bit a pixel): 80 x 25 cells of 8x8; byte (row, line, col) is at
; 0x4000 + line * 0x800 + row * 80 + col (CRTC start 0x3000 = 0xC000, no hardware scroll).
;   F000 draw     (HI_C) char at row HI_ROW, column HI_COL           F003 clrrow  row HI_ROW
;   F006 scroll   rows 1-24 up one, row 24 cleared                    F009 cursor  XOR the cell at HI_ROW, HI_COL
;   F00C init     screen mode, colours and CRTC start, clear the bitmap and the text shadow
;   F00F disk     read / write one 512B sector with the uPD765 (D_TRK, D_ID, D_OP, D_BUF -> D_RES); the kernel's C code is
;                 too big for the 0x8000 limit with this in it, and nothing here needs the screen configuration
; The text shadow (80 x 25 characters at 0xF400) is kept so that tests can read the screen as text.
;   F3F0 HI_C, F3F1 HI_ROW, F3F2 HI_COL, F3F4 saved SP; F3E0-F3E7 the disk variables
;   FC00 font (96 x 8 bytes), FF00 keyboard tables (console.c reads them)
;===================================================================
HI_C    = 0xF3F0
HI_ROW  = 0xF3F1
HI_COL  = 0xF3F2
SPSAVE  = 0xF3F4
D_TRK   = 0xF3E0                ; disk: track, sector id (0x41-0x49), op (0 read, 1 write), buffer, head track + 1 (0 = unknown), result
D_ID    = 0xF3E1
D_OP    = 0xF3E2
D_BUF   = 0xF3E3
D_CYL   = 0xF3E5
D_RES   = 0xF3E6
D_TRY   = 0xF3E7
STK     = 0xF3E0                ; the private stack grows down from here (the disk variables above it are not on it)
SHADOW  = 0xF400
FONT    = 0xFC00
SCR     = 0x4000                ; the screen (bank 3) in configuration 3

        .module hi
        .area   _HI (ABS)
        .org    0xF000
        jp      draw
        jp      clrrow
        jp      scroll
        jp      cursor
        jp      init
        jp      disk

; enter: interrupts off, private stack, configuration 3.  Leaves IFF2 on the stack.
enter:
        pop     hl                      ; return address
        ld      a, i
        push    af
        di
        ld      (SPSAVE), sp
        ld      sp, #STK
        push    hl
        ld      bc, #0x7FC3             ; RAM configuration 3 (banks 0,3,2,7)
        out     (c), c
        ret

; leave: configuration 1 again, the caller's stack, interrupts as they were
leave:
        ld      bc, #0x7FC1             ; RAM configuration 1 (banks 0,1,2,7)
        out     (c), c
        ld      sp, (SPSAVE)
        pop     af
        ret     po
        ei
        ret

; a = row -> hl = a * 80 (de is used)
mul80:
        ld      l, a
        ld      h, #0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      d, h
        ld      e, l
        add     hl, hl
        add     hl, hl
        add     hl, de
        ret

; a = text row -> hl = bitmap address of its first line
rowaddr:
        call    mul80
        ld      de, #SCR
        add     hl, de
        ret

; a = text row -> hl = shadow address of its first cell
shaddr:
        call    mul80
        ld      de, #SHADOW
        add     hl, de
        ret

; hl += HI_COL
addcol:
        ld      a, (HI_COL)
        ld      e, a
        ld      d, #0
        add     hl, de
        ret

; cell address in de (bitmap) for HI_ROW, HI_COL
cell:
        ld      a, (HI_ROW)
        call    rowaddr
        call    addcol
        ex      de, hl
        ret

draw:
        call    enter
        call    cell
        ld      a, (HI_C)
        sub     a, #0x20
        cp      a, #96
        jr      c, 1$
        xor     a, a
1$:     ld      l, a
        ld      h, #0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        ld      bc, #FONT
        add     hl, bc
        ld      b, #8
2$:     ld      a, (hl)
        ld      (de), a
        inc     hl
        ld      a, d
        add     a, #8
        ld      d, a
        djnz    2$
        ; shadow
        ld      a, (HI_ROW)
        call    shaddr
        call    addcol
        ld      a, (HI_C)
        ld      (hl), a
        jp      leave

cursor:
        call    enter
        call    cell
        ld      b, #8
1$:     ld      a, (de)
        cpl
        ld      (de), a
        ld      a, d
        add     a, #8
        ld      d, a
        djnz    1$
        jp      leave

clrrow:
        call    enter
        ld      a, (HI_ROW)
        call    clr1
        jp      leave

; clear text row a (bitmap and shadow)
clr1:
        push    af
        call    rowaddr
        ld      b, #8
1$:     push    hl
        push    bc
        ld      d, h
        ld      e, l
        inc     de
        ld      (hl), #0
        ld      bc, #79
        ldir
        pop     bc
        pop     hl
        ld      a, h
        add     a, #8
        ld      h, a
        djnz    1$
        pop     af
        call    shaddr
        ld      d, h
        ld      e, l
        inc     de
        ld      (hl), #0x20
        ld      bc, #79
        ldir
        ret

scroll:
        call    enter
        ld      hl, #SCR + 80
        ld      de, #SCR
        ld      b, #8
1$:     push    bc
        push    hl
        push    de
        ld      bc, #24 * 80
        ldir
        pop     de
        pop     hl
        pop     bc
        ld      a, h
        add     a, #8
        ld      h, a
        ld      a, d
        add     a, #8
        ld      d, a
        djnz    1$
        ld      hl, #SHADOW + 80
        ld      de, #SHADOW
        ld      bc, #24 * 80
        ldir
        ld      a, #24
        call    clr1
        jp      leave

init:
        call    enter
        ld      bc, #0xBC0C             ; CRTC R12/R13: start 0x3000 (0xC000), offset 0 (the firmware may have scrolled)
        out     (c), c
        ld      bc, #0xBD30
        out     (c), c
        ld      bc, #0xBC0D
        out     (c), c
        ld      bc, #0xBD00
        out     (c), c
        ld      bc, #0x7F00             ; pen 0 black, pen 1 bright white, border black
        out     (c), c
        ld      a, #0x54
        out     (c), a
        ld      c, #0x01
        out     (c), c
        ld      a, #0x4B
        out     (c), a
        ld      c, #0x10
        out     (c), c
        ld      a, #0x54
        out     (c), a
        ld      c, #0x8E                ; mode 2, both ROMs off
        out     (c), c
        ld      hl, #SCR
        ld      de, #SCR + 1
        ld      bc, #0x3FFF
        ld      (hl), #0
        ldir                            ; bitmap
        ld      hl, #SHADOW
        ld      de, #SHADOW + 1
        ld      bc, #25 * 80 - 1
        ld      (hl), #0x20
        ldir
        jp      leave

;---- uPD765: main status 0xFB7E, data 0xFB7F, motor 0xFA7E ----
wr:     push    af
        ld      bc, #0xFB7E
1$:     in      a, (c)
        jp      p, 1$
        inc     c
        pop     af
        out     (c), a
        ret

rd:     ld      bc, #0xFB7E
1$:     in      a, (c)
        jp      p, 1$
        inc     c
        in      a, (c)
        ret

; after a seek or a recalibrate: Sense Interrupt Status until it reports the seek end (an "invalid" 0x80 means nothing
; is pending yet: one result byte only)
sense:  ld      a, #0x08
        call    wr
        call    rd
        ld      e, a
        cp      a, #0x80
        jr      z, sense
        call    rd                      ; PCN
        ld      a, e
        and     a, #0x20
        jr      z, sense
        ret

disk:
        ld      bc, #0xFA7E             ; motor on (boot.s turned it on; it is never turned off)
        ld      a, #1
        out     (c), a
        ld      a, #3
        ld      (D_TRY), a
dtry:   ld      a, (D_CYL)
        or      a, a
        jr      nz, known
        ld      a, #0x07                ; recalibrate
        call    wr
        xor     a, a
        call    wr
        call    sense
        ld      a, #1
        ld      (D_CYL), a
known:  ld      a, (D_TRK)
        ld      b, a
        ld      a, (D_CYL)
        dec     a
        cp      a, b
        jr      z, seekd
        ld      a, #0x0F                ; seek
        call    wr
        xor     a, a
        call    wr
        ld      a, (D_TRK)
        call    wr
        call    sense
        ld      a, (D_TRK)
        inc     a
        ld      (D_CYL), a
seekd:  ld      a, (D_OP)
        or      a, a
        ld      a, #0x46                ; read data, MFM
        jr      z, 1$
        ld      a, #0x45                ; write data, MFM
1$:     call    wr
        xor     a, a                    ; head 0, drive 0
        call    wr
        ld      a, (D_TRK)
        call    wr
        xor     a, a
        call    wr
        ld      a, (D_ID)
        call    wr
        ld      a, #2                   ; 512 bytes
        call    wr
        ld      a, (D_ID)               ; end of track = this sector
        call    wr
        ld      a, #0x2A
        call    wr
        ld      a, #0xFF
        call    wr
        ld      hl, (D_BUF)
        ld      a, (D_OP)
        or      a, a
        jr      nz, wrloop
rdloop: ld      bc, #0xFB7E
        in      a, (c)
        jp      p, rdloop
        and     a, #0x20                ; still in the execution phase?
        jr      z, xdone
        inc     c
        ini
        jr      rdloop
wrloop: ld      bc, #0xFB7E
        in      a, (c)
        jp      p, wrloop
        and     a, #0x20
        jr      z, xdone
        ld      bc, #0xFC7F             ; outi decrements b first
        outi
        jr      wrloop
xdone:  call    rd                      ; ST0
        ld      e, a
        call    rd                      ; ST1
        ld      l, a
        ld      d, #5                   ; ST2 C H R N
2$:     call    rd
        dec     d
        jr      nz, 2$
        ; no terminal count on the CPC: ending at the last sector gives "abnormal termination, end of cylinder"
        ld      a, e
        and     a, #0xC0
        jr      z, ok
        cp      a, #0x40
        jr      nz, bad
        ld      a, l
        cp      a, #0x80
        jr      z, ok
bad:    xor     a, a
        ld      (D_CYL), a              ; recalibrate on the next try
        ld      a, (D_TRY)
        dec     a
        ld      (D_TRY), a
        jp      nz, dtry
        ld      a, #1
        ld      (D_RES), a
        ret
ok:     xor     a, a
        ld      (D_RES), a
        ret

        .area   _HIFONT (ABS)
        .org    0xFC00
        .include "font.inc"

        .area   _HIKEYS (ABS)
        .org    0xFF00
        .include "keys.inc"
