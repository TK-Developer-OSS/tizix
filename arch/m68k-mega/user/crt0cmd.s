/* arch/m68k-mega/user/crt0cmd.s : m68k 外部コマンドの C ランタイム入口
 *
 *   kexec_argv(src/kexec.c + loader.c)が空きスロットへロードし、偽コンテキ
 *   ストで D0=argc / A0=&argv[0] / PC=像の先頭(=ここ)へ「復帰」してくる
 *   (crt0.s の trap0_handler/irq6_handler と同じ movem 復元経路)。
* 
*   #61: **PIC 化した**。-mpcrel でコンパイルした C と揃えるため、この
*   ファイルも PC 相対だけで書く(lea sym(%pc) / bsr.w)。絶対アドレスを
*   1 つでも使うと、ロード先 base がリンク時 VMA(=0)と違った瞬間に壊れる。
*   68000 の PC 相対変位は 16bit = ±32KB なので、像が 32KB 以内である限り届く
*   (IMG_BUDGET は 12KB)。
 *
 *   #48: sh(src/sh.c)が引数をトークンに割って(引用符も処理して)argpack で
 *   渡し、kexec_argv がトークンごとの argv[] を作る。ここは D0/A0 をそのまま
 *   main へ渡すだけ(以前は生文字列 1 本を argv_init.c の build_argv で空白だけ
 *   で割り直していて、`echo "x  y"` の引用符が効かなかった)。
 */
    .globl  _start
_start:
    /* .bss を 0 クリア(電源投入時ではなく、前回このスロットを使った
     * プロセスの残骸が残っているため必須)。 */
    move.l  %d0, %d7           /* argc を退避(次の lea で d0 系は触らない) */
    lea     __bss_start(%pc), %a2
    lea     __bss_end(%pc), %a3
1:
    cmp.l   %a3, %a2
    bge     2f
    clr.b   (%a2)+
    bra     1b
2:
    move.l  %a0, -(%sp)        /* argv (2nd param)。kexec_argv が作った argv[] */
    move.l  %d7, -(%sp)        /* argc (1st param) */
    bsr.w   main
    addq.l  #8, %sp

    /* exit: D0=0 (func) で TRAP #0。D1 = 終了コード(main の戻り値。#111) */
    move.l  %d0, %d1
    moveq   #0, %d0
    trap    #0
4:
    bra     4b

/* ==================================================================
 * 算術ランタイム(旧 user/ulibgcc.s)
 * ==================================================================
 *
 *   #61 で判明した問題: 68000 には 32bit の乗除算命令が無いため、C から
 *   `a * b` / `a / b` / `a % b` を書くと gcc は libgcc の __mulsi3 /
 *   __udivsi3 / __umodsi3 を呼ぶ。ところが **ツールチェーンの libgcc.a は
 *   -mpcrel でビルドされていない**ので、ルーチン同士の呼び出しが絶対
 *   アドレスになっている:
 *
 *     _umodsi3.o:  jsr __udivsi3   <- R_68K_32(絶対)
 *                  jsr __mulsi3    <- R_68K_32(絶対)
 *     _divsi3.o:   jsr __udivsi3   <- R_68K_32(絶対)
 *
 *   これが 1 個でも像に残ると、リンク時 VMA(=0)と違う番地へロードした
 *   瞬間に飛び先が狂う。コマンドを -mpcrel でコンパイルしても libgcc 経由で
 *   PIC が破れるので、必要な分だけ自前で書いて **libgcc をリンクしない**。
 *
 *   ★ここに無いシンボルを C 側が要求したら、リンクが undefined reference で
 *     止まる。それは「libgcc に頼ろうとして PIC が破れる」より望ましい失敗の
 *     仕方なので、黙って libgcc を足さずにここへ PIC 実装を追加すること。
 *     (符号付き __divsi3 / __modsi3 も用意してある。2026-09-24 時点では
 *      どのコマンドも参照していないが、先に置いておく。)
 *
 *   呼出規約: m68k SysV。引数はスタック、戻り値 D0。D0/D1/A0/A1 は
 *   スクラッチ、D2-D7/A2-A6 は callee-saved([[m68k-callee-saved-abi-trap]])。
 *
 *   内部ルーチンの呼び出しは bsr.w(PC 相対)なので PIC が保たれる。
 *   絶対 jsr は 1 つも書かないこと。
 *
 *   ★ここに置いてある理由(2026-09-24 TK 指示)。crt0cmd.o は全コマンドが
 *     必ずリンクするので、**いつでもユーザーコードから使えることが構造的に
 *     保証される**。別オブジェクトに分けると「リンクし忘れたら壊れる」経路が
 *     生まれるが、入口と同居させればそれが起きない。サイズは同じ(以前も
 *     書庫ではなく素の .o を全コマンドがリンクしていた: 202B)。
 */
    .text
    .align  2

    .globl  __mulsi3
    .globl  __udivsi3
    .globl  __umodsi3
    .globl  __divsi3
    .globl  __modsi3

/* ---- unsigned long __mulsi3(unsigned long a, unsigned long b) ----
 *   a*b = al*bl + ((ah*bl + al*bh) << 16)。mulu.w は 16x16->32。
 *   D1 はスクラッチなので保存不要。 */
__mulsi3:
    move.w  4(%sp), %d0          /* a.h */
    mulu.w  10(%sp), %d0         /* a.h * b.l */
    move.w  6(%sp), %d1          /* a.l */
    mulu.w  8(%sp), %d1          /* a.l * b.h */
    add.w   %d1, %d0             /* 上位ワードぶんを合算(桁上げは捨てる) */
    swap    %d0
    clr.w   %d0
    move.w  6(%sp), %d1          /* a.l */
    mulu.w  10(%sp), %d1         /* a.l * b.l */
    add.l   %d1, %d0
    rts

/* ---- 内部: 符号なし 32/32 の商と余り ----
 *   入力  D0 = 被除数、D1 = 除数
 *   出力  D0 = 商、D2 = 余り
 *   破壊  D3
 *   除数 0 は商 0・余り 0 を返す(トラップさせない ── 偽コンテキストで
 *   動くユーザープロセスから 0 除算例外を出すと切り分けが難しくなる)。
 *   復元法(restoring division): 被除数を左へ 1 ビット送り出しながら、
 *   余りレジスタへ X 経由で押し込んで比較・減算する。32 回。 */
.Ludivmod:
    moveq   #0, %d2
    tst.l   %d1
    bne.s   .Ludm_go
    moveq   #0, %d0
    rts
.Ludm_go:
    moveq   #31, %d3
.Ludm_loop:
    lsl.l   #1, %d0              /* 最上位ビットが X へ出る */
    roxl.l  #1, %d2              /* X を余りの最下位へ入れる */
    cmp.l   %d1, %d2             /* CMP は X を壊さないが、ここでは用済み */
    bcs.s   .Ludm_next           /* 余り < 除数 なら商ビット 0 */
    sub.l   %d1, %d2
    addq.l  #1, %d0              /* lsl で空いた最下位へ商ビット 1 */
.Ludm_next:
    dbra    %d3, .Ludm_loop
    rts

/* ---- unsigned long __udivsi3(unsigned long num, unsigned long den) ---- */
__udivsi3:
    movem.l %d2-%d3, -(%sp)      /* 8B: 以降 8(sp)=戻り番地 12(sp)=num 16(sp)=den */
    move.l  12(%sp), %d0
    move.l  16(%sp), %d1
    bsr.w   .Ludivmod
    movem.l (%sp)+, %d2-%d3
    rts

/* ---- unsigned long __umodsi3(unsigned long num, unsigned long den) ---- */
__umodsi3:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d0
    move.l  16(%sp), %d1
    bsr.w   .Ludivmod
    move.l  %d2, %d0             /* 余りを返す */
    movem.l (%sp)+, %d2-%d3
    rts

/* ---- long __divsi3(long num, long den) ----
 *   商の符号 = 被除数と除数の符号の排他。D4 を符号フラグに使う
 *   (0 = 正、-1 = 負。not.l でトグル)。 */
__divsi3:
    movem.l %d2-%d4, -(%sp)      /* 12B: 16(sp)=num 20(sp)=den */
    move.l  16(%sp), %d0
    move.l  20(%sp), %d1
    moveq   #0, %d4
    tst.l   %d0
    bpl.s   .Ldiv_den
    neg.l   %d0
    not.l   %d4
.Ldiv_den:
    tst.l   %d1
    bpl.s   .Ldiv_go
    neg.l   %d1
    not.l   %d4
.Ldiv_go:
    bsr.w   .Ludivmod
    tst.l   %d4
    beq.s   .Ldiv_out
    neg.l   %d0
.Ldiv_out:
    movem.l (%sp)+, %d2-%d4
    rts

/* ---- long __modsi3(long num, long den) ----
 *   C の % は余りの符号を **被除数**に合わせる(C99)。除数の符号は無関係。 */
__modsi3:
    movem.l %d2-%d4, -(%sp)
    move.l  16(%sp), %d0
    move.l  20(%sp), %d1
    moveq   #0, %d4
    tst.l   %d0
    bpl.s   .Lmod_den
    neg.l   %d0
    not.l   %d4                  /* 余りの符号 = 被除数の符号 */
.Lmod_den:
    tst.l   %d1
    bpl.s   .Lmod_go
    neg.l   %d1
.Lmod_go:
    bsr.w   .Ludivmod
    move.l  %d2, %d0
    tst.l   %d4
    beq.s   .Lmod_out
    neg.l   %d0
.Lmod_out:
    movem.l (%sp)+, %d2-%d4
    rts
