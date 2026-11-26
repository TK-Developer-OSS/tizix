;===================================================================
; boot.s - the Amstrad CPC 6128 boot sector (track 0, sector id 0x41, 512 bytes; "system format" disk).
;
; |CPM (typed at the BASIC prompt) makes AMSDOS read it to 0x0100 and jump there.  It copies itself to 0x9000 (above
; where the kernel goes), switches to RAM configuration 1 (banks 0,1,2,7) with both ROMs off, and reads, with the uPD765
; (0xFB7E status, 0xFB7F data) polled, KSECT sectors of 512B from physical sector KFIRST to 0x0100 (the kernel), then
; HSECT sectors to 0xF000 (hi.s), and jumps to 0x0100.
;   Physical sector p = track p / 9, id 0x41 + p % 9.  p = 0 is this sector; the FAT12 volume starts at p = 1, so the
;   kernel (FatFs sector 1 on, in the reserved sectors) is p = 2.
;===================================================================
KFIRST  = 2
KSECT   = 64
HSECT   = 8
HERE    = 0x9000

        .module boot
        .area   _BOOT (ABS)
        .org    HERE
        di
        ld      hl, #0x0100             ; position independent until the jump
        ld      de, #HERE
        ld      bc, #512
        ldir
        jp      go
go:     ld      sp, #HERE
        ld      bc, #0x7FC1             ; RAM configuration 1
        out     (c), c
        ld      bc, #0x7F8E             ; mode 2, both ROMs off
        out     (c), c
        ld      bc, #0xFA7E             ; motor on (AMSDOS has just used the drive)
        ld      a, #1
        out     (c), a
        ld      hl, #0x0100
        ld      a, #KSECT
        call    load
        ld      hl, #0xF000
        ld      a, #HSECT
        call    load
        jp      0x0100

; read a sectors to hl, from the sector in (trk), (sid), advancing
load:
        ld      (cnt), a
1$:     call    rsec
        ld      a, (sid)
        inc     a
        cp      a, #0x4A
        jr      c, 2$
        ld      a, #0x41
        ld      (sid), a
        ld      a, (trk)
        inc     a
        ld      (trk), a
        jr      3$
2$:     ld      (sid), a
3$:     ld      a, (cnt)
        dec     a
        ld      (cnt), a
        jr      nz, 1$
        ret

rsec:
        ld      a, (cyl)
        ld      b, a
        ld      a, (trk)
        cp      a, b
        jr      z, 2$
        ld      (cyl), a
        ld      a, #0x0F                ; seek
        call    wr
        xor     a, a
        call    wr
        ld      a, (trk)
        call    wr
1$:     ld      a, #0x08                ; sense interrupt status until the seek end
        call    wr
        call    rd
        ld      e, a
        cp      a, #0x80
        jr      z, 1$
        call    rd
        ld      a, e
        and     a, #0x20
        jr      z, 1$
2$:     ld      a, #0x46                ; read data, MFM
        call    wr
        xor     a, a                    ; head 0, drive 0
        call    wr
        ld      a, (trk)
        call    wr
        xor     a, a
        call    wr
        ld      a, (sid)
        call    wr
        ld      a, #2                   ; 512 bytes
        call    wr
        ld      a, (sid)                ; end of track = this sector
        call    wr
        ld      a, #0x2A
        call    wr
        ld      a, #0xFF
        call    wr
3$:     ld      bc, #0xFB7E
        in      a, (c)
        jp      p, 3$
        and     a, #0x20                ; execution phase over?
        jr      z, 4$
        inc     c
        ini
        jr      3$
4$:     ld      e, #7                   ; result phase
5$:     call    rd
        dec     e
        jr      nz, 5$
        ret

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

trk:    .db     KFIRST / 9
sid:    .db     0x41 + KFIRST % 9
cyl:    .db     0
cnt:    .db     0
