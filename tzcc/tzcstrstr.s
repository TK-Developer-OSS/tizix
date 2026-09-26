        .module tzcstrstr
        .globl  _strstr

        .area   _CODE

; tzcstr.s から分割 (tizix #31)。ライブラリは .rel 単位でリンクされるので、
; 1 関数のために string 一式 625B を引き込まないよう用途別に切ってある。

_strstr::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
1$:
        push    hl
        ld      e, 4(ix)
        ld      d, 5(ix)
2$:
        ld      a, (de)
        or      a
        jr      z, 4$
        ld      a, (hl)
        or      a
        jr      z, 5$
        ld      a, (de)
        cp      (hl)
        jr      nz, 3$
        inc     hl
        inc     de
        jr      2$
3$:
        pop     hl
        inc     hl
        jr      1$
4$:
        pop     hl
        ret
5$:
        pop     hl
        ld      hl, #0
        ret

; char *memset(char *p, int c, int n)
