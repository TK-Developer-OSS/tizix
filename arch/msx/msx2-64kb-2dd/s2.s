;===================================================================
; s2 - second stage of the boot, linked at 0x8000 (page 2).
;
; The boot sector (boot.s) reads this from logical sector 1 to 0x8000 and jumps here,
; because the boot sector sits in page 3 and could not swap that page's sub-slot.
; It reads the kernel image from the disk (logical sectors 2..65, 512B each) in
; 8KB pieces into 0xA000, with the disk ROM's DSKIO; each piece is copied down to
; 0x0100.. with pages 0 and 1 switched to RAM.  Before that it sets up the text
; screen through the BIOS, and copies the real-time clock's registers to 0x83CB (see
; RTCBUF); at the end it jumps to the kernel at 0x0100.
;   The kernel's disk trampoline (tramp.s) rides here, not in the kernel image (to keep
;   it out of the kernel's 0x0100-0x7FFF): s2 puts it at 0x8C78 first and then uses it
;   itself, for set_page (0x8C7A) and for the kernel reads (0x8C78, parameters at 0x8C68).
;===================================================================
        .module s2
        .area   _S2 (ABS)
        .org    0x8000

KERNEL  = 0x0100
NBYTES  = 0x7F00                ; NSEC * 128, the kernel image size (must match the Makefile)
STAGE   = 0xA000
EXPTBL  = 0xFCC1                ; BIOS: bit 7 = slot 0 is expanded
TRAMP   = 0x8C78                ; tramp.s: the disk call
SETPAGE = 0x8C7A                ; tramp.s: jp set_page (B = page, A = slot id)
P_OP    = 0x8C68                ; tramp.s parameters
P_CNT   = 0x8C69
P_BUF   = 0x8C6A
P_SECT  = 0x8C6C
P_RES   = 0x8C6E
RTCBUF  = 0x83CB                ; kernel block 0's free gap: 'R','T', RP-5C01 block 0 regs 0-12, block 1 reg 10
LINL40  = 0xF3AE                ; BIOS: width of SCREEN 0
CHGMOD  = 0x005F                ; BIOS: set the screen mode (A = mode)

s2:
        di
        ld      sp, #0x9000
        ld      hl, #tramp_image      ; the kernel's disk trampoline (tramp.s) to 0x8C78
        ld      de, #TRAMP
        ld      bc, #tramp_len
        ldir
        ; the text screen first: put the BIOS ROM in page 0 (the caller may have RAM there),
        ; then 80 columns
        ld      b, #0
        ld      a, (EXPTBL)
        and     #0x80           ; main ROM: slot 0 (sub-slot 0 when expanded)
        call    SETPAGE
        ld      a, #80
        ld      (LINL40), a
        xor     a
        call    CHGMOD
        di
        ; the clock (RP-5C01, ports 0xB4 / 0xB5): copy its raw nibbles to RTCBUF for `hwclock`
        ld      a, #13
        out     (0xB4), a
        ld      a, #0x09        ; block 1 (reg 10 bit 0 = 24-hour mode), timer on
        out     (0xB5), a
        ld      a, #10
        out     (0xB4), a
        in      a, (0xB5)
        and     #0x0F
        ld      (RTCBUF + 15), a
        ld      a, #13
        out     (0xB4), a
        ld      a, #0x08        ; block 0
        out     (0xB5), a
        ld      hl, #0x5452     ; 'R','T'
        ld      (RTCBUF), hl
        ld      hl, #RTCBUF + 2
        ld      c, #0
rtc1:   ld      a, c
        out     (0xB4), a
        in      a, (0xB5)
        and     #0x0F
        ld      (hl), a
        inc     hl
        inc     c
        ld      a, c
        cp      #13
        jr      nz, rtc1
        ld      hl, #NBYTES
        ld      (remain), hl
        ld      hl, #KERNEL
        ld      (dest), hl
        ld      hl, #2
        ld      (P_SECT), hl
        ld      hl, #STAGE
        ld      (P_BUF), hl
        ld      hl, #0x1000     ; P_OP = 0 (read), P_CNT = 16 (8KB)
        ld      (P_OP), hl
next:
        call    TRAMP           ; pages 0 and 1 come back as RAM
        ld      a, (P_RES)
        or      a
        jr      nz, fail
        ld      hl, (P_SECT)
        ld      de, #16
        add     hl, de
        ld      (P_SECT), hl
        ; bytes to copy = min(0x2000, remain)
        ld      hl, (remain)
        ld      bc, #0x2000
        or      a
        sbc     hl, bc
        jr      nc, full
        ld      bc, (remain)
        ld      hl, #0
full:
        ld      (remain), hl
        ld      hl, #STAGE
        ld      de, (dest)
        push    bc
        ldir
        ld      (dest), de
        pop     bc
        ld      hl, (remain)
        ld      a, h
        or      l
        jr      nz, next
        jp      KERNEL

fail:
        ; error: leave the sector number's low byte where a debugger can see it and stop
        di
        halt

remain: .dw     0
dest:   .dw     0
        .include "tramp.inc"          ; tramp_image / tramp_len: tramp.s as db lines
