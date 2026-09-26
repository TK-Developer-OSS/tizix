;===================================================================
; crt0cmd.s - tizix external-command fixed entry stub (0x0000 - 0x001F header, _start @ 0x20)
;
; 本モジュールは各プロセスの 0x0000-0x001F を予約ヘッダ、0x0020 から _start。
;
; kexec_argv() がロード時にプロセス最終ブロックへ:
;     [top-0x140, top-0x100)  argv[] 絶対ポインタ配列(末尾 NULL)
;     [top-0x100, top-0x12)   トークン文字列プール
;     [top-0x0E,  top)        偽コンテキスト seed
; を配置し、偽コンテキスト経由で以下のレジスタを与える:
;     IY = base(プロセス先頭ブロック)
;     HL = argc
;     DE = &argv[0](絶対アドレス)
;     BC = nblk(プロセスが占有する連続ブロック数 1..3)
;
; _start は SP をプロセス最終ブロック側(top - 0x140)へ張り直し、
; main(argc, argv) を IY 相対で呼ぶ。--sdcccall 0(引数は右→左で push、
; 戻り値 HL、呼び出し側が後始末)。
;===================================================================

        .module crt0cmd

        .globl  _main
        .globl  _kexit

        .area   _CODE

; 0x00-0x1F : header slot (reserved)
        .ds     0x20

_start::
        ; --- 入口: IY=base, HL=argc, DE=&argv[0](abs), BC=nblk ---

        ; argv(絶対) を IX へ退避(この後の SP 張替を跨いで生かす)
        push    de
        pop     ix              ; IX = &argv[0]

        ; nblk を A に取り出してから、HL(argc)を BC へ退避
        ld      a, c            ; A = nblk
        ld      c, l
        ld      b, h            ; BC = argc

        ; DE = nblk * 0x1000
        add     a, a
        add     a, a
        add     a, a
        add     a, a            ; A = nblk*0x10   (nblk<=4、桁溢れなし)
        ld      d, a
        ld      e, #0           ; DE = nblk*0x1000

        ; HL = base + nblk*0x1000  (= プロセス最終ブロック上端 top)
        push    iy
        pop     hl
        add     hl, de

        ; SP = top - 0x140  (argv[] 配列より下がプロセススタック)
        ld      de, #0xFEC0     ; = -0x140
        add     hl, de
        ld      sp, hl

        ; --- main(argc, argv) を呼ぶ ---
        ; --sdcccall 0: 引数は右→左で push
        push    ix              ; argv
        push    bc              ; argc

        ; 実アドレス (IY + _main) へ jp。戻り番地は実アドレス (IY + ret_from_main)。
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
        jp      (hl)

ret_from_main:
        ; main 復帰 → カーネルへ。argc/argv の push 分は kexit で捨てられる。
        jp      _kexit

; 外部参照の宣言(未参照コマンドでも -g 指定で No definition にならないようにする)
        .globl  _getticks, _time_get, _time_set, _drv_tbl
