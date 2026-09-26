        .module tzcstr
        .globl  _strlen
        .globl  _strcpy
        .globl  _strcat
        .globl  _strcmp
        .globl  _strchr

        .area   _CODE

; tzcstr.s から分割 (tizix #31)。ライブラリは .rel 単位でリンクされるので、
; 1 関数のために string 一式 625B を引き込まないよう用途別に切ってある。

_strlen::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      bc, #0
1$:
        ld      a, (hl)
        or      a
        jr      z, 2$
        inc     bc
        inc     hl
        jr      1$
2$:
        ld      l, c
        ld      h, b
        ret

; char *strcpy(char *dst, const char *src)
_strcpy::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
        push    de
1$:
        ld      a, (hl)
        ld      (de), a
        or      a
        jr      z, 2$
        inc     hl
        inc     de
        jr      1$
2$:
        pop     hl
        ret

; char *strncpy(char *dst, const char *src, int n)
_strcat::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
        push    de
1$:
        ld      a, (de)
        or      a
        jr      z, 2$
        inc     de
        jr      1$
2$:
        ld      a, (hl)
        ld      (de), a
        or      a
        jr      z, 3$
        inc     hl
        inc     de
        jr      2$
3$:
        pop     hl
        ret

; char *strncat(char *dst, const char *src, int n)
_strcmp::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)
        ld      l, 4(ix)
        ld      h, 5(ix)
1$:
        ld      a, (de)
        cp      (hl)
        jr      nz, 3$
        or      a
        jr      z, 2$
        inc     de
        inc     hl
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

; int strncmp(const char *a, const char *b, int n)
_strchr::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      c, 4(ix)
1$:
        ld      a, (hl)
        cp      c
        jr      z, 2$
        or      a
        jr      z, 3$
        inc     hl
        jr      1$
2$:
        ret
3$:
        ld      hl, #0
        ret

; char *strrchr(const char *s, int c)
