        .module tzcout
        .globl  _prs
        .globl  _prnum
        .globl  _prnuml
        .globl  _kputchar

        .area   _CODE

;===================================================================
; 軽量出力 (prs / prnum)。printf を引き込まずに「文字列 + 10 進数」を
; 出したいコマンドのための最小ライブラリ。
;
;   tizix #31: coreutils 22 本のうち 14 本が printf をリンクしていたが、
;   その大半は %s と %d しか使っておらず、実質「文字列を繋いで改行」の
;   ために tzcprintf の 546B を丸ごと払っていた(ls に至っては
;   printf(" <DIR>  ") = 定数文字列で引き込んでいた)。ここを prs/prnum に
;   置き換えると 1 本あたり 546B が浮く。この 2 本で 100B 弱。
;
;   tzcprintf.s と同じ制約で書く:
;     内部 call 無し / 絶対データラベル無し / 分岐は jr・djnz のみ /
;     固定ベクタ(_kputchar 0x3E)呼び出しのみ。
;   よって Tizix の IY 相対 PIC でもそのまま動く(tizix.c は .s を通さない)。
;   引数は tzcc のスタック規約(--sdcccall 0: 右→左 push、呼び出し側が後始末)。
;===================================================================

; void prs(char *s)  -- 文字列を改行なしで出力する(puts は改行を付ける)。
_prs::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a            ; hl = s
_prs_lp:
        ld      a, (hl)
        or      a
        jr      z, _prs_end
        push    hl
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        pop     hl
        inc     hl
        jr      _prs_lp
_prs_end:
        ld      hl, #0
        ret

; void prnum(unsigned n)  -- 0..65535 を 10 進で出力(ゼロ埋め無し、n=0 は "0")。
;   除算は 16 回の復元法シフト除算をインラインで持つ(内部 call も
;   べき乗テーブル = 絶対データラベルも使えないため)。
;   桁は逆順に生成してマシンスタックへ積み、後から取り出して出す。
_prnum::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a            ; hl = n
        ld      c, #0           ; c = 桁数
_pn_gen:
        ; hl = hl / 10、a = hl % 10  (復元法: 16bit を 1 ビットずつ)
        xor     a
        ld      b, #16
_pn_dv:
        add     hl, hl          ; 被除数を左シフト、こぼれた bit が carry
        rla                     ; 剰余 = 剰余*2 + carry
        cp      #10
        jr      c, _pn_nd
        sub     #10
        inc     l               ; 商のビットを立てる(add hl,hl 直後は L bit0=0)
_pn_nd:
        djnz    _pn_dv
        add     a, #48          ; '0' + 剰余
        push    af              ; A は上位バイトに積まれる
        inc     c
        ld      a, h
        or      l
        jr      nz, _pn_gen
_pn_out:
        pop     hl              ; h = 数字文字(push af の上位)
        ld      a, h
        push    bc              ; _kputchar はカーネル C なので BC を潰す
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        pop     bc
        dec     c
        jr      nz, _pn_out
        ld      hl, #0
        ret

; void prnuml(unsigned long v)  -- 0..4294967295 を 10 進で出力。
;   引数は tzcc の long 規約(push de; push hl → 2(sp)=下位16, 4(sp)=上位16)。
;   32 ビットの復元法シフト除算をインラインで持つ。
_prnuml::
        ld      hl, #4
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ex      de, hl          ; de = 上位16
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a            ; hl = 下位16  → de:hl = v
        ld      c, #0           ; c = 桁数
_pl_gen:
        xor     a
        ld      b, #32
_pl_dv:
        add     hl, hl
        rl      e
        rl      d
        rla                     ; 剰余 = 剰余*2 + こぼれた bit
        cp      #10
        jr      c, _pl_nd
        sub     #10
        inc     l
_pl_nd:
        djnz    _pl_dv
        add     a, #48
        push    af
        inc     c
        ld      a, h
        or      l
        or      d
        or      e
        jr      nz, _pl_gen
_pl_out:
        pop     hl
        ld      a, h
        push    bc
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        pop     bc
        dec     c
        jr      nz, _pl_out
        ld      hl, #0
        ret
