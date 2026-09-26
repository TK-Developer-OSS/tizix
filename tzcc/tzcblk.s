; tzcblk.s -- Z80 のブロック命令(LDDR / CPIR / CPDR)を使う塊の操作 (#43)
;
;   **なぜ tzcshare.s と別モジュールにするか**: sdld はライブラリを .rel 単位で
;   引く。同居させると getxbase() しか使わないコマンドまでこの 3 本(約 140B)を
;   背負う(実測: xblk.bin が +95B)。1 ファイル = 1 モジュールに分けておけば、
;   本当に呼んだコマンドだけが払う。libtzc.lib 分離のときと同じ話。
;
;   C の while ループは 1 文字あたり 25 命令 250〜300 T-state。
;   CPIR / CPDR は 21 T-state。走査の多い編集系で効く。

        .module tzcblk
        .globl  _absmovd, _absscan, _absrscan

        .area   _CODE

; void absmovd(unsigned dst, unsigned src, unsigned n) -- 絶対番地間 LDDR (#43)
;   重なりがあって **dst > src**(= 後ろへずらす / 挿入)のときはこちら。
;   absmove(LDIR)でやると自分が書いた先を読んでしまい全部同じ字で埋まる。
;   **先頭番地と長さ**を渡す(末尾は中で作る)。呼ぶ側に n-1 を足させると
;   2 箇所で間違えるため。
_absmovd::
        ld      ix, #0
        add     ix, sp
        ld      c, 6(ix)
        ld      b, 7(ix)        ; BC = n
        ld      a, b
        or      c
        ret     z               ; n == 0 は何もしない(LDDR に 0 を渡すと 64KB 動く)
        dec     bc              ; BC = n-1
        ld      l, 2(ix)
        ld      h, 3(ix)
        add     hl, bc          ; HL = dst + n - 1
        ex      de, hl          ; DE = 転送先の末尾
        ld      l, 4(ix)
        ld      h, 5(ix)
        add     hl, bc          ; HL = src + n - 1
        inc     bc              ; BC = n(カウントへ戻す)
        lddr
        ret

; unsigned absscan(unsigned p, unsigned ch, unsigned n) -- CPIR (#43)
;   [p, p+n) から ch を前方に探し、**見つけた絶対番地**を返す。
;   無ければ終端 p+n を返す。C のループは 1 文字 250〜300 T-state、CPIR は 21。
_absscan::
        ld      ix, #0
        add     ix, sp
        ld      c, 6(ix)
        ld      b, 7(ix)        ; BC = n
        ld      l, 2(ix)
        ld      h, 3(ix)        ; HL = p
        ld      a, b
        or      c
        ret     z               ; n == 0 -> p(= 終端)
        ld      a, 4(ix)        ; A = ch(下位バイトだけ見る)
        cpir                    ; 一致で停止。HL は「一致の次」を指す
        ret     nz              ; 見つからず -> HL = p + n
        dec     hl              ; 見つかった -> 一致位置へ戻す
        ret

; unsigned absrscan(unsigned p, unsigned ch, unsigned n) -- CPDR (#43)
;   [p-n, p) を **後方** に探し、**最後に見つけた ch の 1 つ次**の番地を返す。
;   無ければ範囲の先頭 p-n を返す。「行頭を求める」がそのままこの形になる。
_absrscan::
        ld      ix, #0
        add     ix, sp
        ld      c, 6(ix)
        ld      b, 7(ix)        ; BC = n
        ld      l, 2(ix)
        ld      h, 3(ix)        ; HL = p
        ld      a, b
        or      c
        ret     z               ; n == 0 -> p(= p-n)
        dec     hl              ; 走査開始は p-1
        ld      a, 4(ix)        ; A = ch
        cpdr                    ; 一致で停止。HL は「一致の 1 つ前」を指す
        inc     hl              ; 見つからず: p-n / 見つかった: 一致位置
        ret     nz
        inc     hl              ; 一致の次 = 行頭
        ret
