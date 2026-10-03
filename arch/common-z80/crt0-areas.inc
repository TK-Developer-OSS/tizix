;===================================================================
; arch/common-z80/crt0-areas.inc - エリアの並び順と gsinit(z80pack / z80board 共通)
;
;   arch/<arch>/crt0.s の末尾で .include する。リンカはここに出てきた順に
;   エリアを並べる(コード系 → データ系)。gsinit は初期値つき static の
;   初期値(_INITIALIZER)を RAM 側(_INITIALIZED)へ写す。crt0-init.inc が呼ぶ。
;===================================================================

        .area   _HOME
        .area   _CODE
        .area   _INITIALIZER
        .area   _GSINIT
        .area   _GSFINAL

        .area   _DATA
        .area   _INITIALIZED
        .area   _BSEG
        .area   _BSS
        .area   _HEAP

        .area   _GSINIT
gsinit::
        ld      bc, #l__INITIALIZER
        ld      a, b
        or      a, c
        jr      Z, gsinit_next
        ld      de, #s__INITIALIZED
        ld      hl, #s__INITIALIZER
        ldir
gsinit_next:

        .area   _GSFINAL
        ret
