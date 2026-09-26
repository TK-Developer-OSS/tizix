/* ============================================================================
 * arch/m68k-mega crt0.s
 *   生 MC68000 用ベクタテーブル + 起動コード(GNU as, m68k-elf-gcc 用)。
 *   m68k-elf-gcc は C シンボルに '_' を付けない(nm で確認済み)ので、
 *   .globl も kmain / timer_tick_isr のまま(先頭 _ 無し)。
 *
 *   ベクタ0/1(SSP/PC)はリンカが解決する実アドレスをそのまま置く。
 *   このイメージ全体が Mega によって実機 SRAM へそのまま書き込まれる
 *   (=ROM 相当)ので、値をハードコードし直す必要はない。
 * ========================================================================== */

    .section .vectors,"ax"
    .globl  _vectors
_vectors:
    .long   __stack_top           /* vector 0: 初期 SSP */
    .long   _start                /* vector 1: 初期 PC */
    .long   exc_v2        /* vector 2: bus error */
    .long   exc_v3        /* vector 3: address error */
    .long   exc_v4        /* vector 4: illegal instruction */
    .long   exc_v5        /* vector 5: zero divide */
    .long   exc_v6        /* vector 6: CHK */
    .long   exc_v7        /* vector 7: TRAPV */
    .long   exc_v8        /* vector 8: privilege violation */
    .long   exc_v9        /* vector 9: trace */
    .long   exc_v10        /* vector 10: line 1010 */
    .long   exc_v11        /* vector 11: line 1111 */
    .long   default_vector        /* vector 12: reserved */
    .long   default_vector        /* vector 13: reserved */
    .long   default_vector        /* vector 14: reserved */
    .long   default_vector        /* vector 15: uninitialized interrupt */
    .rept   8
    .long   default_vector        /* vector 16-23: reserved */
    .endr
    .long   exc_v24        /* vector 24: spurious interrupt */
    .long   exc_v25        /* vector 25: autovector level1 */
    .long   exc_v26        /* vector 26: autovector level2 */
    .long   exc_v27        /* vector 27: autovector level3 */
    .long   exc_v28        /* vector 28: autovector level4 */
    .long   exc_v29        /* vector 29: autovector level5 */
    .long   irq6_handler          /* vector 30: autovector level6 (TICK_HZ tick) */
    .long   exc_v31        /* vector 31: autovector level7 (NMI) */
    .long   trap0_handler         /* vector 32: TRAP #0 = kexec syscall */
    .long   trap1_handler         /* vector 33: TRAP #1 = KYIELD(自分から譲る、#94) */
    .rept   222
    .long   default_vector        /* vector 34-255: trap#/未使用 */
    .endr

    .section .text

/* ---- リセットエントリ ---- */
    .globl  _start
_start:
    /* BSS を 0 クリア(電源投入直後の SRAM 内容は不定なので必須) */
    lea     _bss_start, %a0
    lea     _bss_end, %a1
1:
    cmp.l   %a1, %a0
    bge.s   2f
    clr.b   (%a0)+
    bra.s   1b
2:
    jsr     kmain
    /* kmain は本来戻らない。戻ってきたら停止して分かるようにする。 */
3:
    bra.s   3b

/* ---- レベル6周期割込み(TICK_HZ。実機は Mega Timer5、Makefile 参照) ----
 *   src/kernel.c の plt_interrupt() が TICKS++/EPOCH++ を行う共有ハンドラ
 *   (x86-ia16/z80 と同じ)。TICK_HZ=1(Makefile で -DTICK_HZ=1)前提。
 *
 *   #47: kexec 実装に伴い、ここでラウンドロビンのプリエンプションも行う
 *   (x86-ia16 の _isr08 と同じ役回り)。全汎用レジスタを保存 → tick 処理 →
 *   sched_tick_sp(kernel.c)に「今の SP」を渡して「次に走らせる SP」を
 *   受け取る → 差し替えて復帰。A7(SP)自体は movem の対象に含めない
 *   (退避先は kernel.c の sptbl[] で管理する)。 */
    .globl  irq6_handler
irq6_handler:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    jsr     plt_interrupt
    move.l  %sp, %d0
    move.l  %d0, -(%sp)
    jsr     sched_tick_sp
    addq.l  #4, %sp
    move.l  %d0, %sp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

/* ---- TRAP #1: KYIELD(自分から CPU を譲る、#94) ----
 *   irq6_handler と同じ save/pick/restore で次の走行可能スロットへ切り替える。
 *   tick(plt_interrupt)は進めない ── 時間は数えず、順番だけ回す(z80board の
 *   kyield_entry と同じ約束)。退避形式(movem の並び + SR:PC)は irq6_handler と
 *   完全に同じなので、ここで保存したコンテキストへタイマ側から戻っても、その逆でもよい。
 *   カーネルの待ちループ(con_break / kgetchar / getticks / init / proc_block)から
 *   呼ばれ、TRAP #0 の処理の途中(syscall の中)で入ることもある。その場合も
 *   IRQ_ON 済みのところでタイマに割り込まれるのと同じ状況で、新しい危険は増えない。 */
    .globl  trap1_handler
trap1_handler:
    ori.w   #0x0700, %sr
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  %sp, %d0
    move.l  %d0, -(%sp)
    jsr     sched_tick_sp
    addq.l  #4, %sp
    move.l  %d0, %sp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

/* ---- TRAP #0: kexec 用システムコール ----
 *   ユーザーコマンド側(usyscall.s)の規約: D0=func(0=exit) / D1..D4=引数。
 *   戻り値は D0。0=exit だけは特別扱い(現コンテキストを保存せず破棄して
 *   スロットを解放、次を pick して復帰する)。それ以外は sys_call を呼んで
 *   結果を D0 に載せ、同じコンテキストへ戻る(コンテキストスイッチ無し)。
 *
 *   #47 バグ修正: TRAP 命令は(autovector 割込みと違い)SR の割込みマスクを
 *   自動では上げない。マスクしないまま syscall 処理中にレベル6タイマ割込み
 *   が割り込むと、irq6_handler が「今の current(このプロセスのまま)」に
 *   対して sched_tick_sp を呼び、trap0_handler の途中の SP を sptbl[] へ
 *   誤って保存してしまい、次回このプロセスへ復帰した時にスタックが
 *   壊れる(ls / で不定期に再現した「起動直後に戻る」ような暴走の正体)。
 *   trap0_handler の間だけ割込みを禁止し、rte が復帰する SR で元のマスク
 *   (呼び出し元は常に mask=0)へ自動的に戻す。 */
    .globl  trap0_handler
trap0_handler:
    ori.w   #0x0700, %sr
    tst.l   %d0
    bne     tr_normal
    jsr     sched_exit_sp
    move.l  %d0, %sp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte
tr_normal:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  %d4, -(%sp)
    move.l  %d3, -(%sp)
    move.l  %d2, -(%sp)
    move.l  %d1, -(%sp)
    move.l  %d0, -(%sp)
    jsr     m68k_sys_call
    add.l   #20, %sp
    move.l  %d0, (%sp)
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

/* ============================================================================
 * 例外レポータ(実機ブリングアップ用)
 *   元はすべて default_vector の無限ループだったが、実機ではそれが
 *   「電源を入れても何も出ない」という無言ハングにしかならず、原因の
 *   切り分けができない。ベクタごとに番号を D0 に載せる 6 バイトのスタブを
 *   置き、共通ルーチンが UART へ直接
 *       *** EXC nn PC=xxxxxxxx HALT ***
 *   を吐いてから停止する。
 *
 *   C も スタックも信用しない作りにしてある:
 *     ・UART は plat.h の MMIO(0x100000=DATA / 0x100001=STATUS)へ直書き
 *       (con_putc は呼ばない。壊れた状態でも動くのが要件)
 *     ・例外フレームから PC を拾った直後に専用スタック(exc_stk)へ
 *       切り替える。SSP が壊れて落ちた場合でも bsr が二重例外にならない
 *     ・68000 のフレームは グループ0(vec 2,3)が 14 バイト・PC は +10、
 *       それ以外は 6 バイト・PC は +2(68010 以降のフォーマットワードは無い)
 * ========================================================================= */

    .set    UART_D, 0x100000
    .set    UART_S, 0x100001

    .globl  default_vector
default_vector:
    moveq   #0, %d0               /* 0 = 由来不明(12-23 等の予約ベクタ) */
    bra     exc_report

exc_v2:
    moveq   #2, %d0
    bra     exc_report
exc_v3:
    moveq   #3, %d0
    bra     exc_report
exc_v4:
    moveq   #4, %d0
    bra     exc_report
exc_v5:
    moveq   #5, %d0
    bra     exc_report
exc_v6:
    moveq   #6, %d0
    bra     exc_report
exc_v7:
    moveq   #7, %d0
    bra     exc_report
exc_v8:
    moveq   #8, %d0
    bra     exc_report
exc_v9:
    moveq   #9, %d0
    bra     exc_report
exc_v10:
    moveq   #10, %d0
    bra     exc_report
exc_v11:
    moveq   #11, %d0
    bra     exc_report
exc_v24:
    moveq   #24, %d0
    bra     exc_report
exc_v25:
    moveq   #25, %d0
    bra     exc_report
exc_v26:
    moveq   #26, %d0
    bra     exc_report
exc_v27:
    moveq   #27, %d0
    bra     exc_report
exc_v28:
    moveq   #28, %d0
    bra     exc_report
exc_v29:
    moveq   #29, %d0
    bra     exc_report
exc_v31:
    moveq   #31, %d0
    bra     exc_report

exc_report:
    ori.w   #0x0700, %sr          /* これ以上割り込まれない */
    move.l  %d0, %d7              /* D7 = ベクタ番号 */
    move.l  2(%sp), %d6           /* D6 = PC(短フレーム) */
    moveq   #2, %d0
    cmp.l   %d0, %d7
    beq.s   exc_g0
    moveq   #3, %d0
    cmp.l   %d0, %d7
    bne.s   exc_stkok
exc_g0:
    move.l  10(%sp), %d6          /* グループ0 は +10 が PC */
exc_stkok:
    lea     exc_stk_top, %sp      /* 以後 bsr を使ってよい */

    lea     exc_msg1, %a0
    bsr     exc_puts
    move.l  %d7, %d0
    moveq   #2, %d1
    bsr     exc_hex
    lea     exc_msg2, %a0
    bsr     exc_puts
    move.l  %d6, %d0
    moveq   #8, %d1
    bsr     exc_hex
    lea     exc_msg3, %a0
    bsr     exc_puts
exc_halt:
    bra.s   exc_halt

/* A0 = NUL 終端文字列 */
exc_puts:
    move.b  (%a0)+, %d2
    beq.s   exc_puts_end
    bsr.s   exc_putc
    bra.s   exc_puts
exc_puts_end:
    rts

/* D0 = 値, D1 = 桁数(上位から) */
exc_hex:
    subq.l  #1, %d1
    move.l  %d1, %d3
    lsl.l   #2, %d3               /* シフト量 = (桁数-1)*4 */
exc_hex_lp:
    move.l  %d0, %d4
    lsr.l   %d3, %d4
    andi.l  #0x0F, %d4
    cmpi.b  #10, %d4
    blt.s   exc_hex_dig
    addi.b  #('A' - 10), %d4
    bra.s   exc_hex_put
exc_hex_dig:
    addi.b  #'0', %d4
exc_hex_put:
    move.b  %d4, %d2
    bsr.s   exc_putc
    subi.l  #4, %d3
    bpl.s   exc_hex_lp
    rts

/* D2 = 1 文字。TXRDY を待って DATA へ。con_putc に依存しない */
exc_putc:
    btst    #0, UART_S
    beq.s   exc_putc
    move.b  %d2, UART_D
    rts

    .section .rodata
exc_msg1: .asciz "\r\n*** EXC "
exc_msg2: .asciz " PC="
exc_msg3: .asciz " HALT ***\r\n"

    .section .bss
    .even
exc_stk:
    .space  128
exc_stk_top:

    .section .text
