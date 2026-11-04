/* ============================================================================
 * arch/m68k-x68000 crt0.s
 *   Entry point: boot_ipl.s (loaded by the X68000 IPL ROM) puts the kernel at $10000
 *   and jumps here.  The ROM's vector table (address 0: IOCS, exceptions) is kept; only
 *   a few vectors are replaced:
 *     TRAP #0  = kexec syscall      TRAP #1 = KYIELD
 *     $45      = MFP Timer-C (10ms periodic interrupt; chains to the ROM's handler)
 *   TRAP #15 (IOCS) stays the ROM's.  Vectors 2..11 report the exception (see the end).
 * ========================================================================= */

    .section .text.start,"ax"
    .globl  _start
_start:
    ori.w   #0x0700, %sr
    lea     __stack_top, %sp
    lea     _bss_start, %a0
    lea     _bss_end, %a1
1:
    cmp.l   %a1, %a0
    bge.s   2f
    clr.b   (%a0)+
    bra.s   1b
2:
    lea     exc_stubs, %a0              /* vectors 2..11: 8-byte stubs -> exc_report */
    lea     8, %a1
    moveq   #9, %d0
5:  move.l  %a0, (%a1)+
    addq.l  #8, %a0
    dbf     %d0, 5b
    move.l  #trap0_handler, 0x80
    move.l  #trap1_handler, 0x84
    move.l  0x114, old_timerc
    move.l  #timerc_handler, 0x114
    jsr     kmain
3:
    bra.s   3b

    .section .text

/* ---- Timer-C interrupt (TICK_HZ = 100) ----
 *   First call the ROM's handler (IOCS ONTIME, cursor blink, ...) as an interrupt:
 *   push the return address and SR, jump to it, and the ROM's rte comes back to `after`.
 *   Then do the kernel's tick and the round-robin switch. */
    .globl  timerc_handler
timerc_handler:
    pea     after(%pc)
    move.w  %sr, -(%sp)
    move.l  old_timerc, -(%sp)
    rts
after:
/* ---- Body of the periodic interrupt (tick + preemption; TICK_HZ is in the Makefile) ----
 *   src/kernel.c の plt_interrupt() が TICKS++/EPOCH++ を行う共有ハンドラ
 *   (x86-ia16/z80 と同じ)。
 *
 *   #47: kexec 実装に伴い、ここでラウンドロビンのプリエンプションも行う
 *   (x86-ia16 の _isr08 と同じ役回り)。全汎用レジスタを保存 → tick 処理 →
 *   sched_tick_sp(kernel.c)に「今の SP」を渡して「次に走らせる SP」を
 *   受け取る → 差し替えて復帰。A7(SP)自体は movem の対象に含めない
 *   (退避先は kernel.c の sptbl[] で管理する)。 */
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    jsr     plt_interrupt
    tst.l   plat_noswitch               /* during IOCS disk I/O only time advances */
    bne.s   1f
    move.l  %sp, %d0
    move.l  %d0, -(%sp)
    jsr     sched_tick_sp
    addq.l  #4, %sp
    move.l  %d0, %sp
1:  movem.l (%sp)+, %d0-%d7/%a0-%a6
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
 *   スロットを解放、次を pick して復帰する)。それ以外は sys_call(src/sysfile.c)を呼んで
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
    move.l  %d1, -(%sp)        /* 終了コード(#111)。SP はこのあと次のプロセスのものに替わる */
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
    jsr     sys_call
    add.l   #20, %sp
    move.l  %d0, (%sp)
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

    .section .bss
    .even
old_timerc:
    .space  4
    .globl  plat_noswitch
plat_noswitch:
    .space  4

/* ---- IOCS call ----
 *   long iocs_call(long no, long d1, long d2, long d3, void *a1)
 *   Only d0 is a result; d2-d7/a2-a6 are saved here rather than trusting the ROM to keep them. */
    .section .text
    .globl  iocs_call
iocs_call:
    movem.l %d2-%d7/%a2-%a6, -(%sp)
    move.l  48(%sp), %d0
    move.l  52(%sp), %d1
    move.l  56(%sp), %d2
    move.l  60(%sp), %d3
    move.l  64(%sp), %a1
    trap    #15
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

/* ---- Exception reporter ----
 *   The ROM's error screen shows neither the vector nor the PC, so vectors 2..11 are taken
 *   here: print "*** EXC nn PC=xxxxxxxx ***" through IOCS and stop.
 *   The 68000 frame of a bus/address error (2,3) is 14 bytes with the PC at +10; the others
 *   are 6 bytes with the PC at +2. */
    .section .text
    .even
exc_stubs:
    .irp    n, 2,3,4,5,6,7,8,9,10,11
    moveq   #\n, %d0
    bra.s   exc_report
    nop
    nop
    .endr

exc_report:
    ori.w   #0x0700, %sr
    move.l  %d0, %d7
    move.l  2(%sp), %d6
    cmp.l   #2, %d7
    beq.s   1f
    cmp.l   #3, %d7
    bne.s   2f
1:  move.l  10(%sp), %d6
2:  lea     __stack_top, %sp
    lea     exc_msg1(%pc), %a0
    bsr.s   exc_puts
    move.l  %d7, %d0
    moveq   #2, %d1
    bsr.s   exc_hex
    lea     exc_msg2(%pc), %a0
    bsr.s   exc_puts
    move.l  %d6, %d0
    moveq   #8, %d1
    bsr.s   exc_hex
    lea     exc_msg3(%pc), %a0
    bsr.s   exc_puts
3:  bra.s   3b

exc_puts:                           /* a0 = NUL-terminated string */
    moveq   #0, %d1
    move.b  (%a0)+, %d1
    beq.s   4f
    bsr.s   exc_putc
    bra.s   exc_puts
4:  rts

exc_putc:                           /* d1 = one character */
    movem.l %d0-%d3/%a0-%a1, -(%sp)
    moveq   #0x20, %d0
    trap    #15
    movem.l (%sp)+, %d0-%d3/%a0-%a1
    rts

exc_hex:                            /* d0 = value, d1 = number of digits */
    move.l  %d0, %d4
    move.l  %d1, %d5
    subq.l  #1, %d5
6:  move.l  %d5, %d3
    lsl.l   #2, %d3
    move.l  %d4, %d1
    lsr.l   %d3, %d1
    and.l   #15, %d1
    cmp.b   #10, %d1
    blt.s   7f
    addq.b  #7, %d1
7:  add.b   #'0', %d1
    bsr.s   exc_putc
    dbf     %d5, 6b
    rts

exc_msg1: .asciz "\r\n*** EXC "
exc_msg2: .asciz " PC="
exc_msg3: .asciz " ***\r\n"
    .even
