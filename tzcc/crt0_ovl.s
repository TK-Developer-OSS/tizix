;===================================================================
; crt0_ovl.s - オーバーレイ用の入口スタブ (tizix #36)
;
;   オーバーレイは「プロセス」ではなく、**親プロセスの空間へ後から読み込んで
;   呼ぶだけのコード片**。よって crt0_tizix.s のような _start(SP 張り替え /
;   argv 受け取り / kexit)は持たない ── kexit したら親ごと終わってしまう。
;
;   置かれ方:
;     ・sdldz80 -b _CODE=<OVLADDR> でリンクする。**読み込む先のオフセットと
;       一致させること。** tzcc の --tizix-user は「実行時に IY を足す」PIC を
;       吐くので、IY(= 親プロセスのベース)+ リンク時オフセット が実アドレスに
;       なる。ずれると親の別の場所を触る。
;     ・親は base+OVLADDR へ .bin を fread し、その番地を callovl() で呼ぶ。
;
;   このファイルはオフセット 0(= OVLADDR)に置かれ、IY 相対で _main へ飛ぶ。
;   引数は呼び出し側が積んだまま、戻り番地もそのまま = _main の ret が
;   呼び出し側へ返る。ここも手書きなので tizix.c の IY 変換は掛からない。
;===================================================================

        .module crt0_ovl
        .globl  _main

; カーネル低位ベクタは **ここで実体として定義する**(-g では渡さない)。
;   sdld の `-g sym=val` は「参照されている未定義シンボルに値を与える」ものなので、
;   オーバーレイが使わないベクタまで -g で渡すと
;   `No definition of symbol ...` でリンクが落ちる。オーバーレイの中身次第で
;   どれを使うかが変わるため、常に定義しておく方が扱いやすい。
___sdcc_call_hl = 0x0050
_kexit          = 0x003B
_kputchar       = 0x003E
_kgetchar       = 0x0041
_getticks       = 0x0044
        .globl  ___sdcc_call_hl, _kexit, _kputchar, _kgetchar, _getticks

        .area   _CODE

        ld      hl, #_main
        push    iy
        pop     de
        add     hl, de          ; HL = IY + _main(実アドレス)
        jp      (hl)            ; 戻り番地はそのまま = _main の ret で呼出元へ
