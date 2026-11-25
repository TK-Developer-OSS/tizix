.module crt0

;===================================================================
; arch/z80-zxspectrum crt0.s (ZX Spectrum +3).  boot.s reads the kernel image to 0x0100 (all-RAM configuration 0) and jumps to it.
;
;   ZX Spectrum +3 specific here:
;     - all-RAM configuration 0 (1FFD = 0x01): RAM banks 0,1,2,3 at 0000,4000,8000,C000.  Block 7 (0xF000 and up) holds the
;       screen code, font and text shadow (hi.s) and is not touched by the kernel (MEMCHK_TOP_H)
;     - the interrupt is the 50Hz frame interrupt (IM 1), it needs no acknowledge
;   The rest (vectors at 0x0038.., scheduler, init) is shared with the other z80 ports.
;===================================================================

; 0xF000 and above (block 7) is the screen code / font / text shadow (hi.s): neither the memory check
; nor the process slots touch it (MEMCHK_TOP_H in common-z80/crt0-init.inc).
MEMCHK_TOP_H = 0xF0
        .include "../../common-z80/crt0-defs.inc"

		.area   _HEADER (ABS)
        .org    0x0100
        jp      start

        .area   _CODE
start:
        di
        ;
        ld      sp, #0x9000
        im      1
        ld      bc, #0x7FFD
        xor     a
        out     (c), a                ; screen 5, bank 0
        ld      bc, #0x1FFD
        ld      a, #0x09              ; configuration 0, motor on (boot.s left it so)
        out     (c), a
        ; --- the fixed vectors 0x0038-0x006C, copied from a table (vec_table below) ---
        ;     0x0038 isr        0x003B _kexit      0x003E _kputchar   0x0041 _kgetchar
        ;     0x0044 _getticks  0x0047 _kprintf    0x004A _time_get   0x004D _time_set
        ;     0x0050 jp (hl)    (SDCC 4.x helper)
        ;     0x0052 __divuint  0x0055 __moduint   0x0058 __divsint   0x005B __modsint
        ;     0x005E __divulong 0x0061 __mullong   0x0064 __mulint
        ;     0x0067 _memcmp    0x006A _strcmp
        ;   The addresses are fixed: external commands link against them (-g _kputchar=0x003E ...).
        ld      hl, #vec_table
        ld      de, #0x0038
        ld      bc, #0x0035
        ldir

        .include "../../common-z80/crt0-init.inc"

        call    _init
        halt

vec_table:
        jp      isr
        jp      _kexit
        jp      _kputchar
        jp      _kgetchar
        jp      _getticks
        jp      _kprintf
        jp      _time_get
        jp      _time_set
        .db     0xE9, 0xC9              ; 0x0050: jp (hl) ; ret
        jp      __divuint
        jp      __moduint
        jp      __divsint
        jp      __modsint
        jp      __divulong
        jp      __mullong
        jp      __mulint
        jp      _memcmp
        jp      _strcmp

; ---- 0x0038 frame ISR : ticks++ then save/pick/restore ----
        .globl  _kb_isr
isr:
        push    af
        push    bc
        push    de
        push    hl
        call    _kb_isr               ; keyboard scan (C; IX is kept by the callee, IY is reserved)
        pop     hl
        pop     de
        pop     bc
        .include "../../common-z80/crt0-sched.inc"

        .include "../../common-z80/crt0-areas.inc"
