;===================================================================
; crt0_tizix.s - tzcc 外部コマンド用スタートアップ + 最小 libc (bring-up 版)
;
; Tizix プロセスモデル:
;   * .BIN は sdldz80 -b _CODE=0x0000 でリンク。全ラベルは 0 基準オフセット。
;   * kexec がロード先ブロック実ベースを IY にセットし、エントリ base+0x20 へ入る。
;   * 動的アドレス(jp/call/#label/データアクセス)は tzcc が --tizix-user で
;     実行時 IY 加算するコードを生成する。
;   * この手書き分は「_start の IY 相対呼び出し」と「固定ベクタ call」と「jr」だけ
;     で構成し、それ自体は IY 加算不要にしてある(内部 call / 内部 jp / データラベル
;     を使わない)。printf 等の重い libc は後続フェーズで追加。
;
; カーネル低位ベクタ (実行時にカーネルが設置。sdld -g で束縛):
;   0x3B _kexit   0x3E _kputchar   0x41 _kgetchar   0x44 _getticks
;   0x50 ___sdcc_call_hl (= jp (hl))
;===================================================================

        .module crt0_tizix
        .globl  _main
        .globl  _kexit
        .globl  _kputchar
        .globl  _kgetchar
        .globl  _getticks       ; 0x0044。sleep 等が使う。-g _getticks=0x0044(Makefile TZVEC)で解決。
                                ; ここで import 参照を作っておけば未使用コマンドでも -g がエラーにならない。
        .globl  ___sdcc_call_hl
        .globl  _putchar
        .globl  _putc
        .globl  _getchar
        .globl  _puts

        .area   _CODE

; 0x00-0x1F : ヘッダ予約スロット
        .ds     0x20

;-------------------------------------------------------------------
; _start (= オフセット 0x20)
;   kexec_argv 偽コンテキスト: IY=base, HL=argc, DE=&argv[0](abs), BC=nblk
;   (tizix user/crt0cmd.s と同一 ABI。argv[] 配列・文字列プールは kexec が
;    プロセス最終ブロックへ配置済み:
;      [top-0x140, top-0x100)  argv[] 絶対ポインタ配列(末尾 NULL)
;      [top-0x100, ...)        トークン文字列プール )
;   SP を top-0x140 に張り(argv[] より下)、main(argc, argv) を IY 相対で呼ぶ。
;   --sdcccall 0 : 引数は右→左 push、戻り値 HL、呼び出し側が後始末。
;-------------------------------------------------------------------
_start::
        ; argv(絶対) を IX へ退避(SP 張替を跨いで保持)
        push    de
        pop     ix              ; IX = &argv[0]

        ; nblk を A に、argc(HL) を BC へ
        ld      a, c            ; A = nblk
        ld      c, l
        ld      b, h            ; BC = argc

        add     a, a
        add     a, a
        add     a, a
        add     a, a            ; A = nblk*0x10  (nblk<=4、桁溢れ無し)
        ld      d, a
        ld      e, #0           ; DE = nblk*0x1000

        push    iy
        pop     hl
        add     hl, de          ; HL = top = base + nblk*0x1000

        ld      de, #0xFEC0     ; -0x140
        add     hl, de
        ld      sp, hl          ; SP = top - 0x140

        ; main(argc, argv) -- 右→左 push
        push    ix              ; argv
        push    bc              ; argc

        ; 実アドレス (IY + _main) へ jp。戻り番地も実アドレス。
        ld      hl, #_main
        push    iy
        pop     de
        add     hl, de
        push    hl              ; [sp] = 実 _main
        ld      hl, #ret_from_main
        push    iy
        pop     de
        add     hl, de
        ex      (sp), hl        ; [sp] = 実 ret_from_main, hl = 実 _main
        jp      (hl)

ret_from_main:
        jp      _kexit          ; 固定ベクタ 0x3B

;-------------------------------------------------------------------
; int putchar(int c) / putc  -- スタック引数。カーネル _kputchar(0x3E) 素通し。
;   数値即値・固定ベクタ call・jr のみ → IY 加算不要。
;-------------------------------------------------------------------
_putc::
_putchar::
        ld      hl, #2
        add     hl, sp
        ld      a, (hl)
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        ld      hl, #2
        add     hl, sp
        ld      l, (hl)
        ld      h, #0
        ret

;-------------------------------------------------------------------
; int getchar(void) -- カーネル _kgetchar(0x41)
;-------------------------------------------------------------------
_getchar::
        call    _kgetchar
        ret

;-------------------------------------------------------------------
; int puts(const char *s) -- s は呼び出し側が IY 加算済みの実ポインタ。
;   末尾に改行を付ける（標準 C 準拠）。内部 call は固定ベクタ _kputchar のみ、
;   分岐は jr のみ、データラベル無し → IY 加算不要。
;-------------------------------------------------------------------
        .globl  _puts
_puts::
        ld      hl, #2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)
        ex      de, hl          ; hl = s
_puts_lp:
        ld      a, (hl)
        or      a
        jr      z, _puts_nl
        push    hl
        ld      l, a
        ld      h, #0
        push    hl
        call    _kputchar
        pop     af
        pop     hl
        inc     hl
        jr      _puts_lp
_puts_nl:
        ld      hl, #10
        push    hl
        call    _kputchar
        pop     af
        ld      hl, #0
        ret

;-------------------------------------------------------------------
; 乗除算 _mul / _div は tzcmuldiv.s(ライブラリ)へ移した(#69)。
;   ここに置くと crt0_tizix.rel は全コマンドが明示リンクするので、乗除算を
;   書かないコマンド(掟で書かないものが大半)まで 74B を抱えていた。
;-------------------------------------------------------------------

;===================================================================
; DRIVER drv_tbl (0x9000) トランポリン
;
;   tzcc は関数ポインタ codegen 未対応なので、tizix の user/stdio.h に
;   ある ((fn_x_t)drv_tbl[N])(...) マクロは使えない。代わりに固定名の
;   薄いテールジャンプをここに置き、tzcc 側ヘッダは素のプロトタイプに
;   する(未宣言でも tzcc は foo() を call _foo へ落とすので実害なし)。
;
;   drv_tbl は絶対番地 0x9000 の .dw 配列(block1 常駐 DRIVER)。各エントリ
;   は SDCC --sdcccall 0 = tzcc のスタック規約と同一(引数は右→左 push /
;   戻り値 HL / long は DE:HL / 呼び出し側が後始末)。ここは
;   `ld hl,(絶対)` + `jp (hl)` だけ = スタック不変・IY 不要・tizix.c の
;   IY 変換もこの手書き分には掛からない。
;
;   putchar/getchar/puts/printf は上の crt0_tizix 実装(カーネル低位ベクタ
;   0x3E/0x41 経由)をそのまま使う。ここは drv_tbl 実体が要る FS/dir/sched 系のみ。
;
;   addr = 0x9000 + index*2 :
;     fopen=3    fwrite=4   fclose=5   fread=6    fputs=7    fgets=8
;     kbhit=9    getc_timeout=10        fseek=11  ftell=12   feof=13
;     ferror=14  fflush=15  fgetc=16   fputc=17
;     proc_block=19          proc_wake=20
;     opendir=21 readdir=22  closedir=23
;     mkdir=24   unlink=25   rename=26  readdir_size=38
;     klog=46    rsyslog: /var/log/message へ 1 行追記
;===================================================================
        .globl  _fopen, _fread, _fwrite, _fclose, _fputs, _fgets
        .globl  _kbhit, _getc_timeout, _fseek, _ftell, _feof, _ferror, _fflush
        .globl  _fgetc, _fputc
        .globl  _proc_block, _proc_wake
        .globl  _opendir, _readdir, _closedir, _readdir_size
        .globl  _mkdir, _unlink, _rename
        .globl  _pipe_tail          ; #27: drv_tbl[40] 4KB パイプ窓
        .globl  _krun_wait          ; #35: drv_tbl[45] 同期 exec(本体+コマンド分割用)
        .globl  _klog               ; rsyslog: drv_tbl[46]

_fopen::
        ld      hl, (0x9006)
        jp      (hl)
_fwrite::
        ld      hl, (0x9008)
        jp      (hl)
_fclose::
        ld      hl, (0x900A)
        jp      (hl)
_fread::
        ld      hl, (0x900C)
        jp      (hl)
_fputs::
        ld      hl, (0x900E)
        jp      (hl)
_fgets::
        ld      hl, (0x9010)
        jp      (hl)
_kbhit::
        ld      hl, (0x9012)
        jp      (hl)
_getc_timeout::
        ld      hl, (0x9014)
        jp      (hl)
_fseek::
        ld      hl, (0x9016)
        jp      (hl)
_ftell::
        ld      hl, (0x9018)
        jp      (hl)
_feof::
        ld      hl, (0x901A)
        jp      (hl)
_ferror::
        ld      hl, (0x901C)
        jp      (hl)
_fflush::
        ld      hl, (0x901E)
        jp      (hl)
_fgetc::
        ld      hl, (0x9020)
        jp      (hl)
_fputc::
        ld      hl, (0x9022)
        jp      (hl)
_proc_block::
        ld      hl, (0x9026)
        jp      (hl)
_proc_wake::
        ld      hl, (0x9028)
        jp      (hl)
_opendir::
        ld      hl, (0x902A)
        jp      (hl)
_readdir::
        ld      hl, (0x902C)
        jp      (hl)
_closedir::
        ld      hl, (0x902E)
        jp      (hl)
_mkdir::
        ld      hl, (0x9030)
        jp      (hl)
_unlink::
        ld      hl, (0x9032)
        jp      (hl)
_rename::
        ld      hl, (0x9034)
        jp      (hl)
_readdir_size::
        ld      hl, (0x904C)
        jp      (hl)
_pipe_tail::
        ld      hl, (0x9050)
        jp      (hl)
; #35: krun_wait(fname, argpack, argc) = drv_tbl[45]。子を起動して終了まで待つ。
;   tizix にメモリ保護は無いので、子は argv で渡した絶対アドレス経由で親の
;   バッファを直接書ける ── 大きなプログラムを「本体 + コマンド」へ割る入口。
_krun_wait::
        ld      hl, (0x905A)
        jp      (hl)
; rsyslog: klog(msg) = drv_tbl[46]。/var/log/message へ 1 行追記。
_klog::
        ld      hl, (0x905C)
        jp      (hl)

; #35 #36 の補助(getbase / peek / poke / peekw / pokew / absmove / callovl)は
; **tzcshare.s(ライブラリ)へ移した**。ここに置くと crt0_tizix.rel は全コマンドが
; 明示リンクするので、使わないコマンドまで丸ごと太る(実測 +134B。coreutils
; 22 本すべてに乗り、そのぶん各コマンドの実効スタックが減っていた)。
