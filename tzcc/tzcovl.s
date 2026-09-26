;===================================================================
; tzcovl.s - 関数単位オーバーレイのローダ (tizix #69)
;
;   ovlsplit.py が core から「オーバーレイ k の入口関数」への call を
;       ld a, #k / ld hl, #___ovlcall (+IY) / call ___sdcc_call_hl
;   に書き換える。ここはそれを受けて
;     (1) k が今オーバーレイ領域に載っていなければ /bin/<cmd>NN.ovl を
;         領域(___ovlbase)へ読み込み
;     (2) 領域の先頭 = 入口関数へ **jp** する。
;   jp なのでスタックは呼び出し側の call 直後のまま(戻り番地 + 引数)。
;   入口関数の ret はそのまま呼び出し元へ帰る。
;
;   ■ 1 本キャッシュ
;     ___ovlcur に「いま領域に載っている番号」を持つ。同じ k が続く限り
;     一度も読み直さない。読み込み中に失敗しても半端な像を「載っている」と
;     誤認しないよう、読む前に 0 へ落とし、読み終えてから k を書く。
;
;   ■ シンボルの出どころ
;     ___ovlbase … オーバーレイ領域のリンク時オフセット。ovllink.py が
;                  -g で与える(core の終端から決まる)。
;     ___ovlname … "/bin/vi00.ovl" の実体。ovlsplit.py が本体の _DATA へ置く。
;     ___ovlnum  … その "00" の位置。ここで 2 桁を書き換える。
;
;   ■ 触るレジスタ
;     tzcc の呼び出し規約では引数は全てスタック、戻り値は HL。呼び出し直後に
;     tzcc 側が IX を張り直すので、AF/BC/DE/HL/IX は壊してよい。IY は保つ。
;
;   手書きなので tizix.c の IY 変換は掛からない。IY の加算は明示的に行う。
;   drv_tbl(0x9000 の絶対番地)は crt0_tizix と同じく直接引く。
;===================================================================

        .module tzcovl
        .globl  ___ovlcall
        .globl  ___ovlbase, ___ovlname, ___ovlnum
        .globl  ___sdcc_call_hl

        .area   _CODE

; A = オーバーレイ番号 k(1..99)
___ovlcall::
        ld      c, a                    ; C = k(以下ずっと保持)
        ld      hl, #___ovlcur
        push    iy
        pop     de
        add     hl, de                  ; HL = &ovlcur(絶対)
        cp      (hl)
        jr      z, 8$                   ; もう載っている = 読まない
        ld      (hl), #0                ; 読み込み中は「何も載っていない」

        ; ---- 名前の 2 桁を k で書き換える(除算命令が無いので引き算で) ----
        ld      b, #0x2F                ; '0' - 1
1$:     inc     b
        sub     #10
        jr      nc, 1$
        add     a, #0x3A                ; 引き過ぎた 10 を戻して '0' を足す
        ld      hl, #___ovlnum
        push    iy
        pop     de
        add     hl, de
        ld      (hl), b                 ; 十の位
        inc     hl
        ld      (hl), a                 ; 一の位

        push    bc                      ; k を退避(以下の呼び出しで壊れる)

        ; ---- fp = fopen(name, "r") ----
        ld      hl, #9$
        push    iy
        pop     de
        add     hl, de
        push    hl                      ; "r"
        ld      hl, #___ovlname
        push    iy
        pop     de
        add     hl, de
        push    hl                      ; name
        ld      hl, (0x9006)            ; drv_tbl[3] = fopen
        call    ___sdcc_call_hl
        pop     af
        pop     af
        ld      a, h
        or      l
        jr      z, 7$                   ; 開けない = 続行不能

        ; ---- fread(base + ___ovlbase, 1, 0x1000, fp) ----
        push    hl                      ; fclose 用に fp を残す
        push    hl                      ; fp
        ld      hl, #0x1000
        push    hl                      ; n(EOF で止まる。領域に収まることは
                                        ;   ovllink.py がビルド時に保証する)
        ld      hl, #1
        push    hl                      ; size
        ld      hl, #___ovlbase
        push    iy
        pop     de
        add     hl, de
        push    hl                      ; buf
        ld      hl, (0x900C)            ; drv_tbl[6] = fread
        call    ___sdcc_call_hl
        pop     af
        pop     af
        pop     af
        pop     af

        ; ---- fclose(fp) ---- fp はスタックに 1 個残してある
        ld      hl, (0x900A)            ; drv_tbl[5] = fclose
        call    ___sdcc_call_hl
        pop     af

        pop     bc
        ld      hl, #___ovlcur
        push    iy
        pop     de
        add     hl, de
        ld      (hl), c                 ; 読み終えてから「k が載っている」

8$:     ld      hl, #___ovlbase
        push    iy
        pop     de
        add     hl, de
        jp      (hl)                    ; 入口関数へ(スタックは呼出側のまま)

        ; 読み込めなかった(.ovl が /bin に無い等)。続けると領域のゴミへ
        ; 飛ぶので、"ovl?" を出して終了する。main の戻りと同じく HL=戻り値。
7$:     ld      a, #'o'
        call    6$
        ld      a, #'v'
        call    6$
        ld      a, #'l'
        call    6$
        ld      a, #'?'
        call    6$
        ld      hl, #1
        jp      0x003B                  ; kexit(固定ベクタ)

        ; 1 文字出す(kputchar はスタック引数。crt0_tizix の putchar と同じ)
6$:     ld      l, a
        ld      h, #0
        push    hl
        call    0x003E                  ; kputchar(固定ベクタ)
        pop     af
        ret

9$:     .ascii  "r"
        .db     0

        .area   _DATA
___ovlcur:
        .db     0
