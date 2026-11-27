;===================================================================
; boot.s - the ZX Spectrum +3 boot sector (track 0, sector 1, 512 bytes).
;
; The +3 ROM (Loader) reads it to 0xFE00, checks that the sum of its 512 bytes is 3 mod 256 (mkdisk.py fixes the last
; byte) and calls 0xFE10 with RAM configuration 3.  We switch to all-RAM configuration 0 and read, with the uPD765
; (ports 0x2FFD status, 0x3FFD data) polled, KSECT sectors of 512B from physical sector KFIRST to 0x0100 (the kernel),
; then HSECT sectors to 0xF000 (hi.s), and jump to 0x0100.
;   Physical sector p = track p / 9, id p % 9 + 1.  p = 0 is this sector; the FAT12 volume starts at p = 1, so the kernel
;   (FatFs sector 1 on, in the reserved sectors) is p = 2.
;===================================================================
KFIRST  = 2
KSECT   = 64
HSECT   = 6

        .module boot
        .area   _BOOT (ABS)
        .org    0xFE00
        ; the +3 disk specification (single sided, 40 tracks, 9 sectors of 512B, 1 reserved track)
        .db     0x00, 0x00, 40, 9, 2, 1, 3, 2, 0x2A, 0x52, 0, 0, 0, 0, 0, 0
        di
        ld      sp, #0xFE00
        ld      bc, #0x7FFD
        xor     a, a
        out     (c), a                  ; screen 5, bank 0, ROM 0 (all-RAM mode ignores it)
        ld      bc, #0x1FFD
        ld      a, #0x09                ; motor on, special paging, configuration 0 (banks 0,1,2,3)
        out     (c), a
        ld      a, #0x03                ; specify
        call    wr
        ld      a, #0xAF
        call    wr
        ld      a, #0x03                ; step rate / head load, non-DMA
        call    wr
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
        cp      a, #10
        jr      c, 2$
        ld      a, #1
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

; read one sector to hl.  A drive that is not up to speed yet (the motor was just turned on; MAME emulates the spin-up)
; ends the command at once with "not ready": the result phase comes instead of the data, so read the result, wait a
; little and try again.
rsec:
        push    hl
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
1$:     ld      bc, #0x2FFD
        in      a, (c)
        and     a, #0x01                ; drive 0 busy
        jr      nz, 1$
        ld      a, #0x08                ; sense interrupt status
        call    wr
        call    rd
        call    rd
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
        ld      de, #512
3$:     ld      bc, #0x2FFD
4$:     in      a, (c)
        jp      p, 4$
        and     a, #0x20                ; still in the execution phase?
        jr      z, 6$
        ld      b, #0x3F
        ini
        dec     de
        ld      a, d
        or      a, e
        jr      nz, 3$
        ld      e, #7                   ; result phase
5$:     call    rd
        dec     e
        jr      nz, 5$
        pop     de
        ret
6$:     ld      e, #7                   ; failed: read the result, wait, again
7$:     call    rd
        dec     e
        jr      nz, 7$
        pop     hl
        push    hl
        ld      de, #0
8$:     dec     de
        ld      a, d
        or      a, e
        jr      nz, 8$
        jr      2$

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

trk:    .db     KFIRST / 9
sid:    .db     KFIRST % 9 + 1
cyl:    .db     0
cnt:    .db     0
