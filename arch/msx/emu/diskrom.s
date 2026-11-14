;===================================================================
; diskrom - a stand-in disk ROM for the emulator (openMSX + C-BIOS), built into slot 3-1.
; C-BIOS has no working disk ROM and does not boot from disks, so this ROM does what a
; real MSX disk ROM does: it registers drive A in DRVTBL, provides DSKIO at 0x4010 (the
; entry the kernel borrows), and at INIT boots from sector 0 (via loader.s).
; The FDC is a WD2793, memory-mapped the Philips way at 0x7FF8-0x7FFD.
; 720KB 2DD: 9 sectors/track, 2 sides, 512B.   Emulator only; a real MSX has its own.
;===================================================================
        .module diskrom
        .area   _ROM (ABS)
        .org    0x4000

CMD     = 0x7FF8                ; status / command
DAT     = 0x7FFB
SIDE    = 0x7FFC
DRV     = 0x7FFD                ; bit 7 = motor
TRK     = 0x7FF9
SEC     = 0x7FFA

        .db     0x41, 0x42      ; "AB"
        .dw     init
        .dw     0, 0, 0
        .ds     6

        .org    0x4010
        jp      dskio           ; DSKIO
        ret                     ; 0x4013 DSKCHG
        .ds     2
        ret                     ; 0x4016 GETDPB
        .ds     2
        ret                     ; 0x4019 CHOICE
        .ds     2
        ret                     ; 0x401C DSKFMT
        .ds     2
        ret                     ; 0x401F MTOFF

init:
        ld      a, #1
        ld      (0xFB21), a     ; DRVTBL: one drive
        ld      a, #0x87
        ld      (0xFB22), a     ;         in slot 3-1 (0x87)
        ; C-BIOS sets RAMAD0-3 only after the INITs; a real BIOS does it before.  RAM is slot 3-2.
        ld      a, #0x8B
        ld      (0xF341), a
        ld      (0xF342), a
        ld      (0xF343), a
        ld      (0xF344), a
        ld      hl, #loader_image
        ld      de, #0x9000
        ld      bc, #loader_len
        ldir
        jp      0x9000

; DSKIO: B = count, DE = first sector, HL = buffer, carry = write.  Carry = error.
; The sector loop is one macro, assembled twice (read / write).
        .macro  SECTORS wr
        ld      a, #0x80
        ld      (DRV), a        ; motor on, drive A
        xor     a
        ld      (CMD), a        ; restore (track register := 0)
        call    wait
1$:     push    bc
        push    de
        push    hl
        ex      de, hl          ; hl = sector
        ld      d, #0           ; d = track
        ld      bc, #18
2$:     or      a
        sbc     hl, bc
        jr      c, 3$
        inc     d
        jr      2$
3$:     add     hl, bc          ; l = index in the track, 0..17
        ld      a, l
        ld      e, #0           ; e = side
        cp      #9
        jr      c, 4$
        sub     #9
        ld      e, #1
4$:     inc     a
        ld      b, a            ; b = sector number 1..9
        ld      a, e
        ld      (SIDE), a
        ld      a, d
        ld      (DAT), a
        ld      a, #0x10
        ld      (CMD), a        ; seek
        call    wait
        ld      a, b
        ld      (SEC), a
        pop     hl              ; the buffer
        .if     wr
        ld      a, #0xA0        ; write sector
        .else
        ld      a, #0x80        ; read sector
        .endif
        ld      (CMD), a
        ld      bc, #512
5$:     ld      a, (CMD)
        bit     1, a            ; DRQ
        jr      nz, 6$
        rra                     ; busy?
        jr      c, 5$
        jp      rerr
6$:
        .if     wr
        ld      a, (hl)
        ld      (DAT), a
        .else
        ld      a, (DAT)
        ld      (hl), a
        .endif
        inc     hl
        dec     bc
        ld      a, b
        or      c
        jr      nz, 5$
        call    wait
        and     #0x5C           ; not found / CRC / lost data / write protect
        jp      nz, rerr
        pop     de
        pop     bc
        inc     de
        djnz    1$
        or      a
        ret
        .endm

dskio:  jp      c, dskio_w
        SECTORS 0
dskio_w:
        SECTORS 1
rerr:   pop     de
        pop     bc
        ld      a, #0x0C        ; other error
        scf
        ret

; wait until the FDC is not busy; returns its status in A
wait:   ld      a, (CMD)
        rra
        jr      c, wait
        ld      a, (CMD)
        ret

        .include "loader.inc"
