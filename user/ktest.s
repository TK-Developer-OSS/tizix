;===================================================================
; ktest.s - minimal tizix test kernel for cpmsim (linked at 0x0000)
;   cpmsim starts execution at PC=0. This image:
;     - exposes a fixed import vector for commands (_printf/_kexit)
;     - copies the embedded hello image into block1 (0x9000)
;     - relocates the one block-relative code ref
;     - sets iy = block base, jumps to base+0x20
;   hello prints via printf_impl (cpmsim console = OUT port 1),
;   returns, and kexit_impl HALTs (cpmsim terminates).
;===================================================================
        .module ktest
        .area   _CODE

; ---- 0x0000 : entry + command import vector ----------------------
        jp      start           ; 0x0000
        jp      printf_impl      ; 0x0003  <- command _printf import
        jp      kexit_impl       ; 0x0006  <- command _kexit  import

; ---- 0x0009 : loader --------------------------------------------
start:
        ld      sp, #0x9000      ; kernel stack (top of block0)

        ; copy hello image -> block1 (0x9000)
        ld      hl, #hello_img
        ld      de, #0x9000
        ld      bc, #(hello_end - hello_img)
        ldir

        ; relocate the one block-relative code ref (offset 0x29):
        ; word there is 'call _main' target (0x002E); add base.
        ld      hl, (0x9029)
        ld      de, #0x9000
        add     hl, de
        ld      (0x9029), hl

        ; enter command: iy = block base, jump to base+0x20
        ld      iy, #0x9000
        jp      0x9020

; ---- minimal printf: treat arg as a plain string (no % handling) -
; sdcc pushes the fmt pointer; it sits above the return address.
printf_impl:
        ld      hl, #2
        add     hl, sp           ; hl -> pushed fmt pointer
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = fmt pointer
        ex      de, hl           ; hl = fmt pointer
pi_loop:
        ld      a, (hl)
        or      a
        jr      z, pi_done
        out     (1), a           ; cpmsim console data out
        inc     hl
        jr      pi_loop
pi_done:
        ret

kexit_impl:
        halt                     ; cpmsim: HALT ends the emulation

; ---- embedded command image (0x0000-linked hello.bin) -----------
hello_img:
        .incbin "hello.bin"
hello_end:
