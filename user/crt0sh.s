;===================================================================
; crt0sh.s - /bin/sh 専用の入口スタブ(crt0cmd.s の派生)
;
; sh は 3 ブロック(~11.9KB)に肥大し、crt0cmd の SP=top-0x140 では
; 実効スタックが ~70B しか残らず FatFs 呼出しで即オーバーフローする。
; sh は main(argc, argv) の argv を一切参照しない(先頭で (void) キャスト)
; ので、kexec_argv が最終ブロック上端へ置く argv[] 配列(0x40)と
; 文字列プール(0x100)は sh にとって死に領域。ここを丸ごとスタックへ
; 返す ── SP = top - 0x10(偽コンテキスト seed 14B の直下)。
; これで 3 ブロック sh でも ~0x130 余分にスタックが取れる。
;
; crt0cmd.s との違いは SP 初期値のみ。他は完全に同一。
;===================================================================

        .module crt0sh

        .globl  _main
        .globl  _kexit

        .area   _CODE

; 0x00-0x1F : header slot (reserved)
        .ds     0x20

_start::
        ; --- 入口: IY=base, HL=argc, DE=&argv[0](abs), BC=nblk ---

        ; z80board 実機プローブ(2026-09-19 のブリングアップで使用。再度使う時は
        ; 下の ; を外す): 'S' = sh の入口に到達(A は直後に上書きされる)。
        ; 直接ポート出力なので DRIVER/カーネルの出力経路が壊れていても出る。
        ;ld      a, #0x53
        ;out     (0x01), a

        push    de
        pop     ix              ; IX = &argv[0]

        ld      a, c            ; A = nblk
        ld      c, l
        ld      b, h            ; BC = argc

        add     a, a
        add     a, a
        add     a, a
        add     a, a            ; A = nblk*0x10
        ld      d, a
        ld      e, #0           ; DE = nblk*0x1000

        push    iy
        pop     hl
        add     hl, de          ; HL = base + nblk*0x1000 = top

        ; SP = top - 0x10 (argv[]/文字列プールを回収。sh は argv 不使用)
        ld      de, #0xFFF0     ; = -0x10
        add     hl, de
        ld      sp, hl

        ;ld      a, #0x54        ; 実機プローブ 'T' = SP 張り替え完了
        ;out     (0x01), a

        ; --- main(argc, argv) を呼ぶ ---
        push    ix              ; argv
        push    bc              ; argc

        ld      hl, #_main
        push    iy
        pop     de
        add     hl, de
        push    hl
        ld      hl, #ret_from_main
        push    iy
        pop     de
        add     hl, de
        ex      (sp), hl
        ;ld      a, #0x55        ; 実機プローブ 'U' = main へ飛ぶ直前
        ;out     (0x01), a
        jp      (hl)

ret_from_main:
        jp      _kexit

        .globl  _getticks, _time_get, _time_set, _drv_tbl
