;===================================================================
; boot.s - the TRS-80 Model 4P boot sector (track 0, sector id 1, 256 bytes).
;
; The 4P boot ROM reads it to 0x4300 and jumps there.  The kernel goes to 0x0100-0x7FFF, which
; contains 0x4300, so stage A only moves stage B (assembled for 0xF000, which the kernel never
; touches) to 0xF000 and jumps to it.  Stage B reads KSECT sectors of 256B, starting at disk
; sector number KFIRST (track = n / 18, id = n % 18), to 0x0100 with the WD1793 polled (interrupts
; off), then jumps to 0x0100.
;   Disk sector n=0 is id 0 of track 0 (unused), n=1 this sector, n>=2 the FAT volume (2 + 2 * FatFs sector).
;   The kernel image starts at FatFs sector 2: n = 2 + 2*2 = 6.
;===================================================================
KFIRST  = 6
KSECT   = 127
SPT     = 18

        .module boot
        .area   _BOOTA (ABS)
        .org    0x4300
        di
        ld      sp, #0x4200
        ld      a, #6                   ; memory map 2 (RAM 0000-F3FF, keyboard F400, video F800), 80x24
        out     (0x84), a
        xor     a
        out     (0xE4), a               ; no NMI sources
        ld      hl, #0x4320
        ld      de, #0xF000
        ld      bc, #0x00E0
        ldir
        jp      0xF000

        .area   _BOOTB (ABS)
        .org    0xF000
        ld      sp, #0xF3F0
        ld      a, #0x81                ; drive 0, double density
        out     (0xF4), a
        ld      hl, #0x0100             ; destination
        ld      d, #KFIRST / SPT        ; track
        ld      e, #KFIRST % SPT        ; sector id
        ld      c, #KSECT
next:   ld      a, d
        out     (0xF3), a
        ld      a, #0x18                ; seek, no verify
        call    fcmd
        ld      a, e
        out     (0xF2), a
        ld      a, #0x80                ; read sector
        out     (0xF0), a
        ld      b, #100
        djnz    .
rd:     in      a, (0xF0)
        bit     1, a                    ; DRQ
        jr      z, nodrq
        in      a, (0xF3)
        ld      (hl), a
        inc     hl
        jr      rd
nodrq:  rrca
        jr      c, rd                   ; busy
        inc     e
        ld      a, e
        cp      #SPT
        jr      c, same
        ld      e, #0
        inc     d
same:   dec     c
        jr      nz, next
        jp      0x0100

fcmd:   out     (0xF0), a
        ld      b, #100
        djnz    .
w:      in      a, (0xF0)
        rrca
        jr      c, w
        ret
