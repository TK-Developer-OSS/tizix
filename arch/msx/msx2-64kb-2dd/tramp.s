;===================================================================
; tramp - the kernel's way into the disk ROM.  Linked at 0x8C78 (page 2) in the
; free gap of block 0 (kmem.h: 0x8C68-0x8DFF), put there by s2.s (the boot loader).
;
;   0x8C68  op        0 = read, 1 = write
;   0x8C69  count     sectors
;   0x8C6A  buffer    (2 bytes, must be 0x8000 or higher)
;   0x8C6C  sector    (2 bytes, logical sector, 512B)
;   0x8C6E  result    0 = ok, 0xFF = error  (written by the trampoline)
;   0x8C6F  saved SP  (2 bytes)
;   0x8C78  code (this file, at most 224 bytes, up to 0x8D57): 0x8C78 the disk call,
;           0x8C7A jp set_page (for s2.s), 0x8C7D dc_init (clears the VRAM sector
;           cache's tags, see diskio.c)
;   0x8D58  private stack, grows down from 0x8E00 (168 bytes; the stand-in disk ROM's DSKIO
;           was measured at 64)
;
; The caller's interrupt state is restored on return; interrupts are off
; for the whole disk operation (the vblank ticks during it are lost, the clock is
; to be corrected from the RP5C01 afterwards).
;===================================================================
        .module tramp
        .area   _TRAMP (ABS)
        .org    0x8C78

P_OP    = 0x8C68
P_CNT   = 0x8C69
P_BUF   = 0x8C6A
P_SECT  = 0x8C6C
P_RES   = 0x8C6E
P_SP    = 0x8C6F
STACK   = 0x8E00

        jr      tramp                 ; 0x8C78: the disk call (diskio.c, s2.s)
        jp      set_page              ; 0x8C7A: for s2.s (dskio.inc)

; 0x8C7D: every tag of the VRAM sector cache (diskio.c) := 0xFFFF (empty): 512 bytes from
; VRAM 0x07000, one out per loop pass (back-to-back outs are too fast for the VDP).
; Called once by crt0 at start, with interrupts off.  Leaves R#14 = 0.
dc_init:
        ld      a, #1
        out     (0x99), a
        ld      a, #0x8E
        out     (0x99), a             ; R#14 = 1
        xor     a
        out     (0x99), a
        ld      a, #0x70
        out     (0x99), a             ; write from 0x3000 (+ R#14) = 0x07000
        ld      a, #0xFF
        ld      b, #0
1$:     out     (0x98), a
        djnz    1$
2$:     out     (0x98), a
        djnz    2$
        xor     a
        out     (0x99), a
        ld      a, #0x8E
        out     (0x99), a             ; R#14 = 0
        ret

tramp:
        push    ix
        push    iy
        ld      a, i
        jp      pe, 1$
        ld      a, i            ; the NMOS Z80 can report a false 0 here; read twice
1$:     push    af              ; P/V = the interrupt flag
        di
        ld      (P_SP), sp
        ld      sp, #STACK
        ld      hl, (P_BUF)
        ld      de, (P_SECT)
        ld      a, (P_CNT)
        ld      b, a
        ld      a, (P_OP)
        rra                     ; carry = write
        call    do_dskio
        sbc     a, a            ; 0 ok / 0xFF error
        ld      (P_RES), a
        ld      sp, (P_SP)
        pop     af
        jp      po, 2$
        ei
2$:     pop     iy
        pop     ix
        ret

        .include "../dskio.inc"
