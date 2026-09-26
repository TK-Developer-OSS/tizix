        .module tzcmem
        .globl  _memset
        .globl  _memcpy
        .globl  _memmove
        .globl  _memcmp

        .area   _CODE

; tzcstr.s から分割 (tizix #31)。ライブラリは .rel 単位でリンクされるので、
; 1 関数のために string 一式 625B を引き込まないよう用途別に切ってある。

_memset::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)          ; l = 詰める値
        ld      c, 6(ix)
        ld      b, 7(ix)
        push    de
1$:
        ld      a, b
        or      c
        jr      z, 2$
        ld      a, l
        ld      (de), a
        inc     de
        dec     bc
        jr      1$
2$:
        pop     hl
        ret

; char *memcpy(char *dst, const char *src, int n)
_memcpy::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
        ld      c, 6(ix)
        ld      b, 7(ix)
        push    de
1$:
        ld      a, b
        or      c
        jr      z, 2$
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        dec     bc
        jr      1$
2$:
        pop     hl
        ret

; char *memmove(char *dst, const char *src, int n)
_memmove::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
        ld      c, 6(ix)
        ld      b, 7(ix)
        ld      a, b
        or      c
        jr      z, 4$
        push    hl
        or      a
        sbc     hl, de            ; src - dst
        pop     hl
        jr      nc, 1$            ; src >= dst -> 前方コピー
        ; 後方コピー: 末尾から
        add     hl, bc
        dec     hl                ; hl = src + n - 1
        ex      de, hl
        add     hl, bc
        dec     hl                ; hl = dst + n - 1, de = src + n - 1
3$:
        ld      a, (de)
        ld      (hl), a
        dec     hl
        dec     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, 3$
        jr      4$
1$:
        ld      a, (hl)
        ld      (de), a
        inc     hl
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, 1$
4$:
        ld      l, 2(ix)
        ld      h, 3(ix)
        ret

; int memcmp(const char *a, const char *b, int n)
_memcmp::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
        ld      c, 6(ix)
        ld      b, 7(ix)
1$:
        ld      a, b
        or      c
        jr      z, 2$
        ld      a, (de)
        cp      (hl)
        jr      nz, 3$
        inc     de
        inc     hl
        dec     bc
        jr      1$
2$:
        ld      hl, #0
        ret
3$:
        jr      c, 4$
        ld      hl, #1
        ret
4$:
        ld      hl, #-1
        ret
