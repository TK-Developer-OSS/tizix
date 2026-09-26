        .module tzcprintf
        .globl  _printf
        .globl  _itoa
        .globl  _ultoa
        .globl  _kputchar
        .globl  ___sdcc_call_hl

        .area   _CODE

;===================================================================
; 数値整形 libc  (itoa / ultoa / printf)。string/mem/ctype/stdlib は
;
;   すべて「内部 call 無し・絶対データラベル無し・分岐は jr/djnz のみ・
;   固定ベクタ(_kputchar 0x3E)呼び出しのみ」で書いてある。よって Tizix の
;   IY 相対 PIC でもそのまま動く。crt0.s(CP/M) 側の同名ルーチンと論理は共通。
;   引数は tzcc のスタック規約 (2(ix)=第1引数, 4(ix)=第2, ...)。
;===================================================================


; char *itoa(int value, char *buf, int base)   base 16 は 16 進、それ以外は 10 進。
;   10 進のみ負数に '-' を付ける。戻り値 = buf。
_itoa::
        ld      ix, #0
        add     ix, sp
        ld      a, 6(ix)
        cp      #16
        jr      nz, _itoa_dec
; ---- base 16 ----
        ld      e, 2(ix)
        ld      d, 3(ix)          ; de = value (符号なし 16bit)
        ld      c, #0             ; c = 桁数
        ld      a, d
        or      e
        jr      nz, 1$
        ld      l, 4(ix)
        ld      h, 5(ix)
        ld      (hl), #0x30       ; "0"
        inc     hl
        ld      (hl), #0
        jr      _itoa_ret
1$:
        ld      a, e
        and     #0x0f
        push    af
        inc     c
        srl     d
        rr      e
        srl     d
        rr      e
        srl     d
        rr      e
        srl     d
        rr      e
        ld      a, d
        or      e
        jr      nz, 1$
        ld      l, 4(ix)
        ld      h, 5(ix)
2$:
        pop     af
        and     #0x0f
        cp      #10
        jr      c, 3$
        add     a, #0x57          ; 'a'-10
        jr      4$
3$:
        add     a, #0x30
4$:
        ld      (hl), a
        inc     hl
        dec     c
        jr      nz, 2$
        ld      (hl), #0
        jr      _itoa_ret
; ---- base 10 ----
_itoa_dec:
        ld      e, 4(ix)
        ld      d, 5(ix)          ; de = buf 書き込み位置
        ld      l, 2(ix)
        ld      h, 3(ix)          ; hl = value
        bit     7, h
        jr      z, 1$
        ld      a, #0x2d          ; '-'
        ld      (de), a
        inc     de
        push    de
        ex      de, hl
        ld      hl, #0
        or      a
        sbc     hl, de
        pop     de                ; hl = |value|
1$:
        ld      c, #0             ; c = 桁数
2$:
        xor     a
        ld      b, #16
3$:
        add     hl, hl
        rla
        cp      #10
        jr      c, 4$
        sub     #10
        inc     l
4$:
        djnz    3$
        add     a, #0x30
        push    af                ; 剰余(=1桁)を退避
        inc     c
        ld      a, h
        or      l
        jr      nz, 2$
5$:
        pop     af
        ld      (de), a
        inc     de
        dec     c
        jr      nz, 5$
        ld      a, #0
        ld      (de), a
_itoa_ret:
        ld      l, 4(ix)
        ld      h, 5(ix)
        ret

;-------------------------------------------------------------------
; void ultoa(unsigned long value, char *buf, int base)   -- --sdcccall 0
;   2(ix)=下位16  4(ix)=上位16  6(ix)=buf  8(ix)=base(10 or 16)
;   32bit 値 (DE:HL = 上位:下位) を base 10/16 の ASCIIZ にする。負符号なし。
;   register + (hl)/(ix) のみ、分岐 jr/djnz のみ → IY 相対 PIC でそのまま可。
;-------------------------------------------------------------------
_ultoa::
        ld      ix, #0
        add     ix, sp
        ld      l, 2(ix)
        ld      h, 3(ix)          ; hl = 下位16
        ld      e, 4(ix)
        ld      d, 5(ix)          ; de = 上位16   -> DE:HL = 値
        ld      a, 8(ix)
        cp      #16
        jr      z, _ultoa_hex
; ---- base 10 : シフト減算で 32bit /10 を繰り返す ----
        ld      c, #0             ; c = 桁数
1$:
        xor     a                ; a = 剰余アキュムレータ
        ld      b, #32
2$:
        add     hl, hl
        rl      e
        rl      d
        rla
        cp      #10
        jr      c, 3$
        sub     #10
        inc     l                ; 商ビットを立てる
3$:
        djnz    2$
        add     a, #0x30
        push    af               ; 1 桁 (LSD から)
        inc     c
        ld      a, h
        or      l
        or      e
        or      d
        jr      nz, 1$
        ld      l, 6(ix)
        ld      h, 7(ix)          ; hl = buf
4$:
        pop     af               ; LIFO で MSD から取り出す
        ld      (hl), a
        inc     hl
        dec     c
        jr      nz, 4$
        ld      (hl), #0
        ret
; ---- base 16 : 下位ニブルを取り出し 4bit 右シフト、を 8 回 ----
_ultoa_hex:
        ld      c, #8
5$:
        ld      a, l
        and     #0x0f
        cp      #10
        jr      c, 51$
        add     a, #0x37         ; 'A'-10
        jr      52$
51$:
        add     a, #0x30
52$:
        push    af               ; 1 ニブル (LSN から)
        push    bc
        ld      b, #4
53$:
        srl     d
        rr      e
        rr      h
        rr      l
        djnz    53$
        pop     bc
        dec     c
        jr      nz, 5$
        ld      l, 6(ix)
        ld      h, 7(ix)
        ld      c, #8
6$:
        pop     af               ; LIFO で MSN から
        ld      (hl), a
        inc     hl
        dec     c
        jr      nz, 6$
        ld      (hl), #0
        ret

;-------------------------------------------------------------------
; int printf(const char *fmt, ...)
;   書式: %d %i %u %x %X %p %c %s %%  (幅/精度/フラグ/長さ修飾子は読み飛ばし)
;   出力は固定ベクタ _kputchar(0x3E) 経由。数値整形は _itoa へ委譲し、
;   固定ベクタ ___sdcc_call_hl(0x50) 経由の IY 相対 間接CALL で呼ぶ
;   (crt0_tizix 内部の直接 call / 絶対 jp を避けるため。分岐は jr のみ)。
;   SDCC 版 _kputchar は IX/IY を保存する契約なので IX をフレームに使える。
;
;   スタックフレーム (ix を locals 底に置き、参照は全て正オフセット):
;     0(ix)..11(ix)  = _itoa 整形バッファ (12B)
;    12(ix),13(ix)   = fmt カーソル
;    14(ix),15(ix)   = 次可変引数ポインタ
;    16(ix)..19(ix)  = 予備
;    20(ix),21(ix)   = 退避した ix
;    22(ix),23(ix)   = 戻り番地
;    24(ix),25(ix)   = fmt
;    26(ix)...       = 可変引数
;-------------------------------------------------------------------
_printf::
        push    ix
        ld      hl, #-20
        add     hl, sp
        ld      sp, hl            ; locals 20 バイト
        ld      ix, #0
        add     ix, sp            ; ix = locals 底
        ld      l, 24(ix)
        ld      h, 25(ix)
        ld      12(ix), l
        ld      13(ix), h         ; fmt カーソル = fmt
        push    ix
        pop     hl
        ld      de, #26
        add     hl, de
        ld      14(ix), l
        ld      15(ix), h         ; 可変引数ポインタ = &arg1
_pf_l:
        ld      l, 12(ix)
        ld      h, 13(ix)
        ld      a, (hl)
        inc     hl
        ld      12(ix), l
        ld      13(ix), h
        or      a
        jr      z, _pf_done
        cp      #0x25             ; '%'
        jr      nz, _pf_put1
        xor     a
        ld      16(ix), a         ; 'l'(long) 修飾子フラグ クリア
_pf_spec:
        ld      l, 12(ix)
        ld      h, 13(ix)
        ld      a, (hl)
        inc     hl
        ld      12(ix), l
        ld      13(ix), h
        or      a
        jr      z, _pf_done
        cp      #0x25             ; "%%"
        jr      z, _pf_put1
        cp      #0x41             ; < 'A' : フラグ/幅/精度/'.'/数字 -> 読み飛ばし
        jr      c, _pf_spec
        cp      #0x6c             ; 'l' 長さ修飾子 -> long フラグ
        jr      nz, _pf_notl
        ld      a, #1
        ld      16(ix), a
        jr      _pf_spec
_pf_notl:
        cp      #0x68             ; 'h' 長さ修飾子 -> 読み飛ばし
        jr      z, _pf_spec
        ; a = 変換文字。可変引数(下位16)を de へ取り出す (a は保持)。
        ; long の上位16 は _pf_num32 側で 14/15(ix) を進めて取る。
        ld      l, 14(ix)
        ld      h, 15(ix)
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        inc     hl
        ld      14(ix), l
        ld      15(ix), h
        cp      #0x63             ; 'c'
        jr      z, _pf_chr
        cp      #0x73             ; 's'
        jr      z, _pf_str
        cp      #0x78             ; 'x'
        jr      z, _pf_n16
        cp      #0x58             ; 'X'
        jr      z, _pf_n16
        cp      #0x70             ; 'p'
        jr      z, _pf_n16
        ; 'd' 'i' 'u' およびその他 -> 10 進
        jr      _pf_n10

; ---- 中央リレー: ここから ±127 で全分岐先に届く ----
_pf_put1:
        ; A = 出力する 1 文字。出力後メインループへ戻る。
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
_pf_back:
        jr      _pf_l
_pf_done:
        ld      hl, #20
        add     hl, sp
        ld      sp, hl            ; locals 20B 破棄 -> sp = 退避 ix
        pop     ix
        ld      hl, #0
        ret
_pf_chr:
        ld      a, e
        jr      _pf_put1
_pf_str:
        ; de = 表示する asciiz へのポインタ
_pf_str_lp:
        ld      a, (de)
        or      a
        jr      z, _pf_back
        push    de
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        pop     de
        inc     de
        jr      _pf_str_lp

_pf_n10:
        ld      c, #10
        jr      _pf_num
_pf_n16:
        ld      c, #16
_pf_num:
        ; de = 下位16 / c = 基数。16(ix) が立っていれば long(_ultoa)、そうでなければ
        ; 16bit(_itoa)。どちらも 4 ワード push して pop af x4 で揃える。
        ld      b, #0
        push    ix                ; [保存] printf フレーム
        ld      a, 16(ix)
        or      a
        jr      nz, _pf_num_long
        ; --- 16bit: [ダミー][基数][&buf][値] ---
        push    de                ; ダミー(pop で捨てる)
        push    bc                ; 基数
        push    ix
        pop     hl
        push    hl                ; &buf
        push    de                ; 値
        ld      hl, #_itoa
        jr      _pf_num_call
_pf_num_long:
        ; --- 32bit: 上位16 を今取る。[基数][&buf][上位16][下位16] ---
        ld      l, 14(ix)
        ld      h, 15(ix)
        ld      a, (hl)
        inc     hl
        ld      17(ix), a
        ld      a, (hl)
        inc     hl
        ld      18(ix), a
        ld      14(ix), l
        ld      15(ix), h
        push    bc                ; 基数
        push    ix
        pop     bc
        push    bc                ; &buf
        ld      c, 17(ix)
        ld      b, 18(ix)
        push    bc                ; 上位16
        push    de                ; 下位16
        ld      hl, #_ultoa
_pf_num_call:
        push    iy
        pop     de
        add     hl, de
        call    ___sdcc_call_hl   ; = jp (hl) ; ret  (固定ベクタ 0x50)
        pop     af
        pop     af
        pop     af
        pop     af
        pop     ix                ; [復元] printf フレーム
        push    ix
        pop     de                ; de = &buf
        jr      _pf_str_lp
