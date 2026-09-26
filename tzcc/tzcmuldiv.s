;===================================================================
; tzcmuldiv.s - 乗除算 _mul / _div (tzcc が `*` `/` と要素サイズの掛け算で呼ぶ)
;
;   もとは crt0_tizix.s にあった(中身は 1 バイトも変えていない)。crt0_tizix.rel は
;   全コマンドが明示リンクするので、乗除算を書かないコマンド(tizix の掟で
;   書かないのが大半)まで 74B を抱えていた。#69 で vi を 2 ブロックへ詰める
;   ときにライブラリへ出した。**参照したコマンドだけ**が引く。
;
;   レジスタのみ(データラベル・内部 call 無し → IY 加算不要)。
;     HL = DE * HL   /   HL = DE / HL
;   引数: 4(sp)=左, 2(sp)=右   (呼び出し側が pop af x2)。HL は入力に使わない
;   (--tizix-user の間接 CALL 変換が HL を潰すため)。
;===================================================================

        .module tzcmuldiv
        .globl  _mul
        .globl  _div

        .area   _CODE

_mul::
        ld      hl, #4
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)
        ld      hl, #0
_mul_lp:
        ld      a, d
        or      e
        ret     z
        bit     0, e
        jr      z, _mul_sh
        add     hl, bc
_mul_sh:
        srl     d
        rr      e
        sla     c
        rl      b
        jr      _mul_lp

_div::
        ld      hl, #4
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ld      hl, #2
        add     hl, sp
        ld      c, (hl)
        inc     hl
        ld      b, (hl)
        ld      hl, #0
        ld      a, #16
_div_lp:
        sla     e
        rl      d
        adc     hl, hl
        or      a
        sbc     hl, bc
        jr      nc, _div_ge
        add     hl, bc
        jr      _div_nx
_div_ge:
        inc     e
_div_nx:
        dec     a
        jr      nz, _div_lp
        ex      de, hl
        ret
