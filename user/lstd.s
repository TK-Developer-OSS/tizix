;===================================================================
; lstd.s - tizix user-command stdlib.h library (hand PIC assembler)
;
;   Replaces the C-compiled stdlib.c on the command build path.
;   Rules (this module is NOT run through iy_reg_claude.py):
;     * branches: only jr / djnz  (PC-relative -> base independent).
;       NO  jp <label> / call <label>.
;     * static _DATA symbols link at an absolute address (past _CODE);
;       at run time the command sits at IY (block base), so a static
;       pointer needs IY added by hand:
;           ld hl,#sym / push iy / pop de / add hl,de
;       (Reading IY is allowed; the iron law only forbids repurposing
;        it.  crt0cmd.s reads IY the same way.)
;     * IX is caller-saved -> if used, push/pop it.
;   ABI: --sdcccall 0.  args: 2(sp)=arg1lo.. ; caller pops args.
;        return: HL (16-bit) ; long: DE:HL (DE=high word).
;===================================================================
        .module lstd
        .globl _atoi, _atol, _abs, _labs, _rand, _srand, _itoa

        .area _DATA
_rand_seed:  .ds 2            ; only static state; itoa uses stack locals

        .area _CODE

;===========================================================
; int atoi(const char *s)
;===========================================================
_atoi::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = s
1$:                              ; skip ' ' '\t' '\n' '\r'
        ld      a, (de)
        cp      #0x20
        jr      z, 2$
        cp      #0x09
        jr      z, 2$
        cp      #0x0a
        jr      z, 2$
        cp      #0x0d
        jr      z, 2$
        jr      3$
2$:
        inc     de
        jr      1$
3$:
        ld      c, #0            ; c = negative flag
        ld      a, (de)
        cp      #0x2d            ; '-'
        jr      nz, 4$
        ld      c, #1
        inc     de
        jr      5$
4$:
        cp      #0x2b            ; '+'
        jr      nz, 5$
        inc     de
5$:
        ld      hl, #0           ; hl = accumulator
6$:
        ld      a, (de)
        sub     #0x30
        jr      c, 9$
        cp      #10
        jr      nc, 9$
        push    de
        push    af               ; save digit
        ld      d, h
        ld      e, l
        add     hl, hl           ; x2
        add     hl, hl           ; x4
        add     hl, de           ; x5
        add     hl, hl           ; x10
        pop     af
        ld      e, a
        ld      d, #0
        add     hl, de           ; + digit
        pop     de
        inc     de
        jr      6$
9$:
        ld      a, c
        or      a
        ret     z                ; positive -> hl
        ex      de, hl
        ld      hl, #0
        xor     a
        sbc     hl, de           ; hl = -acc
        ret

;===========================================================
; long atol(const char *s)   returns DE:HL
;   32-bit accumulator kept in DE:HL during the digit loop,
;   sign flag on the stack.
;===========================================================
_atol::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ex      de, hl           ; de = s
1$:
        ld      a, (de)
        cp      #0x20
        jr      z, 2$
        cp      #0x09
        jr      z, 2$
        cp      #0x0a
        jr      z, 2$
        cp      #0x0d
        jr      z, 2$
        jr      3$
2$:
        inc     de
        jr      1$
3$:
        ld      a, (de)
        cp      #0x2d
        jr      nz, 31$
        ld      c, #1
        inc     de
        jr      33$
31$:
        cp      #0x2b
        jr      nz, 32$
        inc     de
32$:
        ld      c, #0
33$:
        push    bc               ; [sp] = sign flag in C
        push    de               ; [sp] = s ptr
        ld      hl, #0           ; acc low
        ld      d, h
        ld      e, l             ; acc high (de) = 0
4$:
        pop     bc               ; bc = s ptr (via bc)
        ld      a, (bc)
        inc     bc
        push    bc               ; save advanced s ptr
        sub     #0x30
        jr      c, 8$
        cp      #10
        jr      nc, 8$
        ; --- DE:HL = DE:HL*10 + A ---
        push    af               ; save digit
        ; x2
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        ex      de, hl           ; DE:HL = acc*2
        push    de
        push    hl               ; stash acc*2
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        ex      de, hl           ; *4
        add     hl, hl
        ex      de, hl
        adc     hl, hl
        ex      de, hl           ; *8
        pop     bc               ; bc = (acc*2).lo
        add     hl, bc
        ex      de, hl
        pop     bc               ; bc = (acc*2).hi
        adc     hl, bc
        ex      de, hl           ; DE:HL = acc*10
        pop     af               ; digit
        ld      c, a
        ld      b, #0
        add     hl, bc
        ld      a, e
        adc     a, #0
        ld      e, a
        ld      a, d
        adc     a, #0
        ld      d, a             ; + digit (carry propagated)
        jr      4$
8$:
        pop     bc               ; discard s ptr
        pop     bc               ; bc = sign flag (C)
        ld      a, c
        or      a
        ret     z                ; positive -> DE:HL
        ; negate 32-bit DE:HL
        ld      a, #0
        sub     l
        ld      l, a
        ld      a, #0
        sbc     a, h
        ld      h, a
        ld      a, #0
        sbc     a, e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
        ret

;===========================================================
; int abs(int j)
;===========================================================
_abs::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a             ; hl = j
        bit     7, h
        ret     z
        ex      de, hl
        ld      hl, #0
        xor     a
        sbc     hl, de
        ret

;===========================================================
; long labs(long j)   returns DE:HL
;===========================================================
_labs::
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = low word
        inc     hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = high word
        bit     7, d
        jr      nz, 1$
        ld      l, c
        ld      h, b
        ret
1$:
        xor     a
        sub     c
        ld      l, a
        ld      a, #0
        sbc     a, b
        ld      h, a
        ld      a, #0
        sbc     a, e
        ld      e, a
        ld      a, #0
        sbc     a, d
        ld      d, a
        ret

;===========================================================
; int rand(void)   seed = seed*25173 + 13849 ; return seed & 0x7FFF
;===========================================================
_rand::
        ld      hl, #_rand_seed
        push    iy
        pop     de
        add     hl, de           ; hl = &_rand_seed (real)
        push    hl
        ld      e, (hl)
        inc     hl
        ld      d, (hl)          ; de = seed
        ld      hl, #0           ; hl = product
        ld      bc, #25173
1$:
        srl     b
        rr      c
        jr      nc, 2$
        add     hl, de
2$:
        sla     e
        rl      d
        ld      a, b
        or      c
        jr      nz, 1$
        ld      de, #13849
        add     hl, de           ; hl = new seed (mod 65536)
        pop     de               ; de = &_rand_seed
        ld      a, l
        ld      (de), a
        inc     de
        ld      a, h
        ld      (de), a
        res     7, h
        ret

;===========================================================
; void srand(unsigned seed)
;===========================================================
_srand::
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)          ; bc = seed
        ld      hl, #_rand_seed
        push    iy
        pop     de
        add     hl, de
        ld      (hl), c
        inc     hl
        ld      (hl), b
        ret

;===========================================================
; char *itoa(int value, char *str, int radix)
;   IX frame pointer; digit buffer (18 B) + 4 scalars live as stack
;   locals (SP-relative -> position independent, no static data).
;   relative to IX:  (ix-18 .. ix-1) buffer, &buf[k] = ix-18+k
;                    (ix-19) radix  (ix-20) neg  (ix-21) cnt  (ix-22) bit
;===========================================================
_itoa::
        push    ix
        ld      ix, #0
        add     ix, sp
        ld      hl, #-24
        add     hl, sp
        ld      sp, hl           ; 24 bytes of locals

        ld      c, 4 (ix)
        ld      b, 5 (ix)        ; bc = value
        ld      a, 8 (ix)        ; a = radix low byte

        sub     #2               ; validate radix 2..16
        cp      #15
        jr      c, 2$
        ld      l, 6 (ix)
        ld      h, 7 (ix)
        ld      (hl), #0         ; *str = '\0'
        ld      sp, ix
        pop     ix
        ret
2$:
        add     a, #2
        ld      -19 (ix), a      ; radix
        ld      -20 (ix), #0     ; neg
        ld      -21 (ix), #0     ; cnt
        cp      #10
        jr      nz, 4$
        bit     7, b
        jr      z, 4$
        ld      -20 (ix), #1     ; neg = 1
        xor     a
        sub     c
        ld      c, a
        ld      a, #0
        sbc     a, b
        ld      b, a
4$:
        ld      a, b
        or      c
        jr      nz, 5$
        ld      -18 (ix), #0x30  ; buf[0] = '0'
        ld      -21 (ix), #1     ; cnt = 1
        jr      8$
5$:
        ld      -22 (ix), #16    ; bit counter
        ld      h, #0            ; remainder
        ld      l, -19 (ix)      ; divisor
6$:
        sla     c
        rl      b
        rl      h
        ld      a, h
        sub     l
        jr      c, 7$
        ld      h, a
        inc     c
7$:
        dec     -22 (ix)
        jr      nz, 6$
        ld      a, h
        cp      #10
        jr      c, 71$
        add     a, #0x61 - 10
        jr      72$
71$:
        add     a, #0x30
72$:                             ; a = digit ; bc = quotient (keep)
        push    bc
        ld      c, -21 (ix)      ; cnt
        ld      b, #0
        push    ix
        pop     hl
        ld      de, #-18
        add     hl, de
        add     hl, bc           ; hl = &buf[cnt]
        ld      (hl), a
        ld      a, -21 (ix)
        inc     a
        ld      -21 (ix), a      ; cnt++
        pop     bc
        ld      a, b
        or      c
        jr      nz, 5$
8$:
        ld      e, 6 (ix)
        ld      d, 7 (ix)        ; de = str cursor
        ld      a, -20 (ix)
        or      a
        jr      z, 9$
        ld      a, #0x2d         ; '-'
        ld      (de), a
        inc     de
9$:
        ld      a, -21 (ix)      ; cnt
10$:
        dec     a
        push    af
        ld      l, a
        ld      h, #0
        push    ix
        pop     bc
        push    bc
        ld      bc, #-18
        add     hl, bc
        pop     bc
        add     hl, bc           ; hl = ix-18+k
        ld      a, (hl)
        ld      (de), a
        inc     de
        pop     af
        or      a
        jr      nz, 10$
        xor     a
        ld      (de), a          ; NUL
        ld      l, 6 (ix)
        ld      h, 7 (ix)        ; return str
        ld      sp, ix
        pop     ix
        ret
