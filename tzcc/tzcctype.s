        .module tzcctype
        .globl  _isdigit
        .globl  _isalpha
        .globl  _isalnum
        .globl  _isspace
        .globl  _isupper
        .globl  _islower
        .globl  _isxdigit
        .globl  _isprint
        .globl  _isgraph
        .globl  _iscntrl
        .globl  _ispunct
        .globl  _toupper
        .globl  _tolower

        .area   _CODE

; ---- ctype ----  (int -> int, 真偽は 1 / 0)
_isdigit::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        sub     #0x30
        jr      c, 1$
        cp      #10
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_isupper::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        sub     #0x41
        jr      c, 1$
        cp      #26
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_islower::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        sub     #0x61
        jr      c, 1$
        cp      #26
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_isalpha::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        or      #0x20
        sub     #0x61
        jr      c, 1$
        cp      #26
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_isalnum::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        sub     #0x30
        jr      c, 1$
        cp      #10
        jr      c, 2$
1$:
        ld      a, 2(ix)
        or      #0x20
        sub     #0x61
        jr      c, 3$
        cp      #26
        jr      nc, 3$
2$:
        ld      hl, #1
        ret
3$:
        ld      hl, #0
        ret

_isspace::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        cp      #0x20
        jr      z, 1$
        sub     #0x09
        jr      c, 2$
        cp      #5
        jr      c, 1$
        jr      2$
1$:
        ld      hl, #1
        ret
2$:
        ld      hl, #0
        ret

_isxdigit::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        sub     #0x30
        jr      c, 1$
        cp      #10
        jr      c, 2$
1$:
        ld      a, 2(ix)
        or      #0x20
        sub     #0x61
        jr      c, 3$
        cp      #6
        jr      nc, 3$
2$:
        ld      hl, #1
        ret
3$:
        ld      hl, #0
        ret

_isprint::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        cp      #0x20
        jr      c, 1$
        cp      #0x7f
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_isgraph::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        cp      #0x21
        jr      c, 1$
        cp      #0x7f
        jr      nc, 1$
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_iscntrl::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        cp      #0x7f
        jr      z, 1$
        cp      #0x20
        jr      c, 1$
        ld      hl, #0
        ret
1$:
        ld      hl, #1
        ret

_ispunct::
        ld      ix, #0
        add     ix, sp
        ld      a, 2(ix)
        cp      #0x21
        jr      c, 1$
        cp      #0x7f
        jr      nc, 1$
        cp      #0x30
        jr      c, 2$
        cp      #0x3a
        jr      c, 1$
        cp      #0x41
        jr      c, 2$
        cp      #0x5b
        jr      c, 1$
        cp      #0x61
        jr      c, 2$
        cp      #0x7b
        jr      c, 1$
2$:
        ld      hl, #1
        ret
1$:
        ld      hl, #0
        ret

_toupper::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      a, l
        sub     #0x61
        jr      c, 1$
        cp      #26
        jr      nc, 1$
        ld      a, l
        sub     #0x20
        ld      l, a
        ld      h, #0
1$:
        ret

_tolower::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      a, l
        sub     #0x41
        jr      c, 1$
        cp      #26
        jr      nc, 1$
        ld      a, l
        add     a, #0x20
        ld      l, a
        ld      h, #0
1$:
        ret
