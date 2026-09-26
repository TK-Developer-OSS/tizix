        .module tzcstrn
        .globl  _strncpy
        .globl  _strncat
        .globl  _strncmp
        .globl  _strrchr
        .globl  _memchr

        .area   _CODE

; tzcstr.s から分割 (tizix #31)。ライブラリは .rel 単位でリンクされるので、
; 1 関数のために string 一式 625B を引き込まないよう用途別に切ってある。

_strncpy::
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
        jr      z, 3$
        ld      a, (hl)
        ld      (de), a
        or      a
        jr      z, 2$
        inc     hl
        inc     de
        dec     bc
        jr      1$
2$:
        ; NUL に達した後は残りを 0 で埋める (C 標準)
        dec     bc
        ld      a, b
        or      c
        jr      z, 3$
        inc     de
        xor     a
        ld      (de), a
        jr      2$
3$:
        pop     hl
        ret

; char *strcat(char *dst, const char *src)
_strncat::
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
        ld      a, (de)
        or      a
        jr      z, 2$
        inc     de
        jr      1$
2$:
        ld      a, b
        or      c
        jr      z, 3$
        ld      a, (hl)
        or      a
        jr      z, 3$
        ld      (de), a
        inc     hl
        inc     de
        dec     bc
        jr      2$
3$:
        xor     a
        ld      (de), a
        pop     hl
        ret

; int strcmp(const char *a, const char *b)
_strncmp::
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
        or      a
        jr      z, 2$
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

; char *strchr(const char *s, int c)
_strrchr::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      c, 4(ix)
        ld      de, #0
1$:
        ld      a, (hl)
        cp      c
        jr      nz, 2$
        ld      d, h
        ld      e, l
2$:
        or      a
        jr      z, 3$
        inc     hl
        jr      1$
3$:
        ex      de, hl
        ret

; char *memchr(const char *s, int c, int n)
_memchr::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      c, 6(ix)
        ld      b, 7(ix)
1$:
        ld      a, b
        or      c
        jr      z, 2$
        ld      a, 4(ix)
        cp      (hl)
        jr      z, 3$
        inc     hl
        dec     bc
        jr      1$
2$:
        ld      hl, #0
3$:
        ret

; char *strstr(const char *hay, const char *needle)
