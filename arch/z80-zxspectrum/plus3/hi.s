;===================================================================
; hi.s - the screen code of the ZX Spectrum +3 port, assembled for 0xF000 (loaded there by boot.s, never touched by the kernel).
;
; The kernel runs in all-RAM configuration 0 (banks 0,1,2,3).  The screen is bank 5, which only exists at 0x4000 in
; configuration 2 (banks 4,5,6,3).  Bank 3 at 0xC000 is the same in both, so everything that runs while the
; screen is paged in lives up here: interrupts off, a private stack, switch, draw, switch back.
;   F000 draw     (HI_C) char at row HI_ROW, column HI_COL           F003 clrrow  row HI_ROW
;   F006 moverow  row HI_ROW -> row HI_ROW-1                         F009 cursor  XOR the cell at HI_ROW, HI_COL
;   F00C init     clear the bitmap, set the attributes, clear the text shadow
;   F00F disk     read / write one 512B sector with the uPD765 (D_TRK, D_ID, D_OP, D_BUF -> D_RES); the kernel's C code is
;                 too big for the 0x8000 limit with this in it, and nothing here needs the screen configuration
; The text shadow (32 x 24 characters at 0xF400) is kept so that tests can read the screen as text.
;   F3F0 HI_C, F3F1 HI_ROW, F3F2 HI_COL, F3F4 saved SP, F3F6 MOTOR (0x08 once a disk was used; kept in the 1FFD writes)
;===================================================================
HI_C    = 0xF3F0
HI_ROW  = 0xF3F1
HI_COL  = 0xF3F2
SPSAVE  = 0xF3F4
MOTOR   = 0xF3F6
D_TRK   = 0xF3E0                ; disk: track, sector id (1-9), op (0 read, 1 write), buffer, head track + 1 (0 = unknown), result
D_ID    = 0xF3E1
D_OP    = 0xF3E2
D_BUF   = 0xF3E3
D_CYL   = 0xF3E5
D_RES   = 0xF3E6
D_TRY   = 0xF3E7
STK     = 0xF3E0                ; the private stack grows down from here (the disk variables above it are not on it)
SHADOW  = 0xF400
FONT    = 0xF800

        .module hi
        .area   _HI (ABS)
        .org    0xF000
        jp      draw
        jp      clrrow
        jp      moverow
        jp      cursor
        jp      init
        jp      disk

; enter: interrupts off, private stack, configuration 2.  Leaves IFF2 on the stack.
enter:
        pop     hl                      ; return address
        ld      a, i
        push    af
        di
        ld      (SPSAVE), sp
        ld      sp, #STK
        push    hl
        ld      a, (MOTOR)
        or      a, #0x05                ; special paging, configuration 2 (banks 4,5,6,3)
        ld      bc, #0x1FFD
        out     (c), a
        ret

; leave: configuration 0 again, the caller's stack, interrupts as they were
leave:
        ld      a, (MOTOR)
        or      a, #0x01
        ld      bc, #0x1FFD
        out     (c), a
        ld      sp, (SPSAVE)
        pop     af
        ret     po
        ei
        ret

; a = text row -> hl = bitmap address of its first line (0x4000 + (row & 0x18) * 0x100 + (row & 7) * 32)
rowaddr:
        ld      b, a
        and     a, #0x18
        or      a, #0x40
        ld      h, a
        ld      a, b
        and     a, #7
        rrca
        rrca
        rrca
        ld      l, a
        ret

; a = text row -> hl = shadow address of its first cell
shaddr:
        ld      l, #0
        rrca
        rrca
        rrca
        ld      h, a                    ; row >> 3 in the low bits, (row & 7) << 5 in the high bits
        and     a, #0xE0
        ld      l, a
        ld      a, h
        and     a, #0x03
        add     a, #>SHADOW
        ld      h, a
        ret

; cell address in de (bitmap) for HI_ROW, HI_COL
cell:
        ld      a, (HI_ROW)
        call    rowaddr
        ld      a, (HI_COL)
        or      a, l
        ld      e, a
        ld      d, h
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
        inc     d
        djnz    2$
        ; shadow
        ld      a, (HI_ROW)
        call    shaddr
        ld      a, (HI_COL)
        add     a, l
        ld      l, a
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
        inc     d
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
        ld      bc, #31
        ldir
        pop     bc
        pop     hl
        inc     h
        djnz    1$
        pop     af
        call    shaddr
        ld      d, h
        ld      e, l
        inc     de
        ld      (hl), #0x20
        ld      bc, #31
        ldir
        ret

moverow:
        call    enter
        ld      a, (HI_ROW)
        dec     a
        call    rowaddr
        push    hl                      ; destination row
        ld      a, (HI_ROW)
        call    rowaddr                 ; hl = source row
        pop     de
        ld      b, #8
1$:     push    bc
        push    hl
        push    de
        ld      bc, #32
        ldir
        pop     de
        pop     hl
        pop     bc
        inc     h
        inc     d
        djnz    1$
        ld      a, (HI_ROW)
        dec     a
        call    shaddr
        push    hl
        ld      a, (HI_ROW)
        call    shaddr
        pop     de                      ; hl = source, de = destination
        ld      bc, #32
        ldir
        jp      leave

init:
        call    enter
        ld      hl, #0x4000
        ld      de, #0x4001
        ld      bc, #6143
        ld      (hl), #0
        ldir                            ; bitmap
        ld      hl, #0x5800
        ld      de, #0x5801
        ld      bc, #767
        ld      (hl), #0x07             ; white on black
        ldir
        ld      hl, #SHADOW
        ld      de, #SHADOW + 1
        ld      bc, #767
        ld      (hl), #0x20
        ldir
        xor     a, a
        out     (0xFE), a               ; black border
        jp      leave

;---- uPD765 ----
wr:     push    af
        ld      bc, #0x2FFD
1$:     in      a, (c)
        jp      p, 1$
        ld      b, #0x3F
        pop     af
        out     (c), a
        ret

rd:     ld      bc, #0x2FFD
1$:     in      a, (c)
        jp      p, 1$
        ld      b, #0x3F
        in      a, (c)
        ret

; wait for the drive, then Sense Interrupt Status (two result bytes)
sense:  ld      bc, #0x2FFD
1$:     in      a, (c)
        and     a, #0x01
        jr      nz, 1$
        ld      a, #0x08
        call    wr
        call    rd
        jp      rd

disk:
        ld      a, #0x08
        ld      (MOTOR), a
        ld      a, #0x09                ; motor on, configuration 0
        ld      bc, #0x1FFD
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
rdloop: ld      bc, #0x2FFD
        in      a, (c)
        jp      p, rdloop
        and     a, #0x20                ; still in the execution phase?
        jr      z, xdone
        ld      b, #0x3F
        ini
        jr      rdloop
wrloop: ld      bc, #0x2FFD
        in      a, (c)
        jp      p, wrloop
        and     a, #0x20
        jr      z, xdone
        ld      b, #0x40                ; outi decrements b first
        outi
        jr      wrloop
xdone:  call    rd                      ; ST0
        ld      e, a
        ld      d, #6                   ; ST1 ST2 C H R N
2$:     call    rd
        dec     d
        jr      nz, 2$
        ld      a, e
        and     a, #0xC0
        jr      z, ok
        xor     a, a
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
        .org    0xF800
        .include "font.inc"
