        .module tzcstdlib
        .globl  _atoi
        .globl  _atol
        .globl  _abs
        .globl  _labs

        .area   _CODE

; ---- stdlib ----
; long atol(const char *s) / int atoi(const char *s)   (16bit サブセット)
_atol::
_atoi::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
1$:
        ld      a, (hl)
        cp      #32
        jr      z, 2$
        cp      #9
        jr      z, 2$
        jr      3$
2$:
        inc     hl
        jr      1$
3$:
        ld      c, #0
        cp      #45
        jr      nz, 4$
        ld      c, #1
        inc     hl
        jr      5$
4$:
        cp      #43
        jr      nz, 5$
        inc     hl
5$:
        push    bc                ; 符号フラグ(c) をループ中の bc 破壊から退避
        ld      de, #0
6$:
        ld      a, (hl)
        sub     #48
        jr      c, 7$
        cp      #10
        jr      nc, 7$
        push    hl
        ld      l, a
        ld      h, #0
        push    hl
        ld      h, d
        ld      l, e
        add     hl, hl
        ld      b, h
        ld      c, l
        add     hl, hl
        add     hl, hl
        add     hl, bc
        pop     bc
        add     hl, bc
        ex      de, hl
        pop     hl
        inc     hl
        jr      6$
7$:
        ex      de, hl
        pop     bc                ; 符号フラグ復元
        ld      a, c
        or      a
        ret     z
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        ret

; long labs(long) / int abs(int)
_labs::
_abs::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        bit     7, h
        ret     z
        ld      a, l
        cpl
        ld      l, a
        ld      a, h
        cpl
        ld      h, a
        inc     hl
        ret
