;===================================================================
; tzcshare.s - プロセス間共有メモリ / オーバーレイ呼び出しの補助 (#35 #36)
;
;   **ライブラリモジュールにしてある**のが要点。crt0_tizix.s に置くと
;   crt0_tizix.rel は全コマンドが明示リンクするので、使わないコマンドまで
;   丸ごと太る(実測 +134B。coreutils 22 本すべてに乗り、そのぶん各コマンドの
;   実効スタックが減る)。ライブラリなら **参照したものだけ**が引かれる。
;
;   いずれも IY を触らない素の手書きなので tizix.c の IY 変換の対象外。
;   どの番地へリンクされても動く = オーバーレイからも使える。
;
;   引数は --sdcccall 0(右→左 push / 戻り値 HL / 呼び出し側が後始末)。
;   IX は tzcc 側が呼出し後に張り直すので壊してよい。
;===================================================================

;   peek / poke / callovl は tzcpeek.s へ分けた(#69)。sdld はモジュール単位で
;   引くので、同じモジュールにあると語しか使わない vi まで 57B を抱える。
        .module tzcshare
        .globl  _getbase, _peekw, _pokew, _absmove
        .globl  _getxbase, _getxsize

        .area   _CODE

; unsigned getxbase(void) -- 追加ブロック(作業領域)の先頭絶対番地 (#38)
;   kexec がロード時にヘッダ 0x1C-0x1D へ imgtop(像の占有サイズ)を書く。
;   追加ブロックを宣言していなければ像の直後 = 使ってはいけない番地を返すので、
;   getxsize() が 0 でないことを確かめてから使うこと。
_getxbase::
        push    iy
        pop     hl
        ld      de, #0x001C
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a            ; HL = imgtop
        push    iy
        pop     de
        add     hl, de          ; HL = base + imgtop
        ret

; unsigned getxsize(void) -- 追加ブロックの合計バイト数 (#38)。0 = 宣言していない
;   (nblk - imgtop/4096) * 4096 を、乗除算を使わずシフトで作る。
_getxsize::
        push    iy
        pop     hl
        ld      de, #0x001E
        add     hl, de
        ld      a, (hl)         ; A = nblk(合計)
        push    iy
        pop     hl
        ld      de, #0x001D
        add     hl, de
        ld      h, (hl)         ; H = imgtop の上位 = 像のブロック数*0x10
        srl     h
        srl     h
        srl     h
        srl     h               ; H = 像のブロック数
        sub     h               ; A = 追加ブロック数
        ld      h, a
        ld      l, #0           ; HL = 追加ブロック数 * 0x100
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl          ; HL = 追加ブロック数 * 0x1000
        ret

; unsigned getbase(void) -- 自プロセスのベースアドレス(IY)
_getbase::
        push    iy
        pop     hl
        ret

; unsigned peekw(unsigned addr) -- tzcc は `int *` を参照できないので語はこれ
_peekw::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ret

; void pokew(unsigned addr, unsigned v)
_pokew::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)
        ld      e, 4(ix)
        ld      d, 5(ix)
        ld      (hl), e
        inc     hl
        ld      (hl), d
        ret

; void absmove(unsigned dst, unsigned src, unsigned n)
_absmove::
        ld      ix, #0
        add     ix, sp
        ld      e, 2(ix)
        ld      d, 3(ix)        ; DE = dst
        ld      l, 4(ix)
        ld      h, 5(ix)        ; HL = src
        ld      c, 6(ix)
        ld      b, 7(ix)        ; BC = n
        ld      a, b
        or      c
        ret     z
        ldir
        ret

