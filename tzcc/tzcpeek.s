;===================================================================
; tzcpeek.s - 1 バイトの peek / poke と callovl (#35 #36)
;
;   もとは tzcshare.s に同居していた。sdld はライブラリを **モジュール単位**で
;   引くので、語の peekw / pokew や absmove しか使わないコマンド(vi)まで
;   この 57B を抱えていた。#69 で vi を 2 ブロックへ詰めるときに分けた。
;   中身は 1 バイトも変えていない。
;
;   IY を触らない素の手書きなので tizix.c の IY 変換の対象外。
;   引数は --sdcccall 0(右→左 push / 戻り値 HL / 呼び出し側が後始末)。
;   IX は tzcc 側が呼出し後に張り直すので壊してよい。
;===================================================================

        .module tzcpeek
        .globl  _peek, _poke, _callovl
        .globl  ___sdcc_call_hl

        .area   _CODE

; unsigned char peek(unsigned addr)
_peek::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      l, (hl)
        ld      h, #0
        ret

; void poke(unsigned addr, unsigned char v)
_poke::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      a, 4(ix)
        ld      (hl), a
        ret

; unsigned callovl(unsigned addr, unsigned arg)  -- #36 オーバーレイ呼び出し
;   tzcc は関数ポインタを生成できないので、実行時に決まる番地への call は
;   ここを通す。addr は絶対番地(自プロセスのベース + OVLADDR)。
_callovl::
        ld      ix, #0
        add     ix, sp
        ld      l, 4(ix)
        ld      h, 5(ix)
        push    hl              ; overlay の main へ渡す引数
        ld      l, 2(ix)
        ld      h, 3(ix)        ; HL = overlay の絶対エントリ
        call    ___sdcc_call_hl
        pop     de              ; 積んだ引数を捨てる(HL = 戻り値は保つ)
        ret
