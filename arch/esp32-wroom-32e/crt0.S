/* ============================================================================
 * arch/esp32-wroom-32e crt0.S
 *   ベクタ表 / 入口 / 例外入口(タイマ割込み・syscall・文脈切替)。
 *
 *   カーネルは windowed ABI(#109: WiFi のバイナリと ROM の関数が windowed なので、
 *   カーネルもそれに揃えた。以前は call0 で、ROM は rom_call で窓を一時的に作って呼んでいた)。
 *   外部コマンドは call0 のまま(syscall 命令の例外で入るので、ABI は混ざらない)。
 *
 *   文脈の切替(FreeRTOS の Xtensa 版と同じ考え方):
 *     1. 例外入口で a0..a15 と特殊レジスタを、割り込まれた側のスタックにフレームとして積む。
 *     2. 生きている窓(呼び出し元たちのレジスタ)を全部それぞれのスタックへ吐き出す(SPILL)。
 *        これで「今の窓」だけが残り、その中身はもうフレームにある。
 *     3. 別のスロットのフレームを降ろして rfe。戻った先の関数が retw すると、窓の
 *        戻し(underflow)例外で呼び出し元のレジスタがスタックから読み戻される。
 *   フレームの大きさ CTX_SIZE は、割り込まれた関数の SP の直下 16 バイト(呼び出し元の
 *   a0..a3 が吐き出される場所)を潰さないように取ってある(plat.h)。
 *
 *   フレームの形は include/plat.h の CTX_*(loader.c の偽コンテキストと共通)。
 * ========================================================================== */

/* 64 本のレジスタ(16 窓)を一回りして、生きている窓を全部あふれさせる。
 * PS.WOE = 1、PS.EXCM = 0 で実行すること(あふれは下の WindowOverflow が処理する)。 */
    .macro  SPILL_ALL_WINDOWS
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    3
    and     a12, a12, a12
    rotw    4
    .endm

/* ---- ベクタ表(VECBASE。1KB 境界に置く。番地は ESP32 の LX6 の構成で固定) ---- */
    .section .vectors, "ax"
    .global _vectors
_vectors:

    .macro  BADVEC off, id
    .org    \off
    movi    a2, \id
    j       vec_fatal
    .endm

/* 窓のあふれ / 戻し(Xtensa の標準の処理)。呼び先の SP の 16 バイト下に a0..a3、
 * 8 / 12 本の窓では残りを「呼び出し元の呼び出し元の SP」の下に置く。 */
    .org    0x000
_WindowOverflow4:
    s32e    a0, a5, -16
    s32e    a1, a5, -12
    s32e    a2, a5, -8
    s32e    a3, a5, -4
    rfwo
    .org    0x040
_WindowUnderflow4:
    l32e    a0, a5, -16
    l32e    a1, a5, -12
    l32e    a2, a5, -8
    l32e    a3, a5, -4
    rfwu
    .org    0x080
_WindowOverflow8:
    s32e    a0, a9, -16
    l32e    a0, a1, -12
    s32e    a2, a9, -8
    s32e    a1, a9, -12
    s32e    a3, a9, -4
    s32e    a4, a0, -32
    s32e    a5, a0, -28
    s32e    a6, a0, -24
    s32e    a7, a0, -20
    rfwo
    .org    0x0C0
_WindowUnderflow8:
    l32e    a1, a9, -12
    l32e    a0, a9, -16
    l32e    a7, a1, -12
    l32e    a2, a9, -8
    l32e    a4, a7, -32
    l32e    a3, a9, -4
    l32e    a5, a7, -28
    l32e    a6, a7, -24
    l32e    a7, a7, -20
    rfwu
    .org    0x100
_WindowOverflow12:
    s32e    a0, a13, -16
    l32e    a0, a1, -12
    s32e    a1, a13, -12
    s32e    a2, a13, -8
    s32e    a3, a13, -4
    s32e    a4, a0, -48
    s32e    a5, a0, -44
    s32e    a6, a0, -40
    s32e    a7, a0, -36
    s32e    a8, a0, -32
    s32e    a9, a0, -28
    s32e    a10, a0, -24
    s32e    a11, a0, -20
    rfwo
    .org    0x140
_WindowUnderflow12:
    l32e    a1, a13, -12
    l32e    a0, a13, -16
    l32e    a11, a1, -12
    l32e    a2, a13, -8
    l32e    a4, a11, -48
    l32e    a8, a11, -32
    l32e    a3, a13, -4
    l32e    a5, a11, -44
    l32e    a6, a11, -40
    l32e    a7, a11, -36
    l32e    a9, a11, -28
    l32e    a10, a11, -24
    l32e    a11, a11, -20
    rfwu

    BADVEC  0x180, 0x22         /* Level2 割込み */
    BADVEC  0x1C0, 0x23         /* Level3 割込み */
    BADVEC  0x200, 0x24         /* Level4 割込み */
    BADVEC  0x240, 0x25         /* Level5 割込み */
    BADVEC  0x280, 0x26         /* Debug */
    BADVEC  0x2C0, 0x27         /* NMI */
    .org    0x300               /* Kernel 例外(PS.UM=0 のとき) */
    j       exc_entry
    .org    0x340               /* User 例外(PS.UM=1。レベル 1 割込みと syscall はここ) */
    j       exc_entry
    BADVEC  0x3C0, 0x2D         /* Double 例外(例外処理中の例外) */
    .org    0x400

/* ---- 入口(ROM の 1 段目ローダから。戻らない) ---- */
    .section .text.entry, "ax"
    .global _start
    .type   _start, @function
    .align  4
_start:
    /* 割込みを止め、窓を作り直す(WindowBase = 0、今の窓だけ生きている)。 */
    movi    a2, PS_UM | 15
    wsr.ps  a2
    rsync
    movi    a2, 0
    wsr.intenable a2
    wsr.lcount a2
    movi    a2, _vectors
    wsr.vecbase a2
    movi    a2, 0
    wsr.windowbase a2
    rsync
    movi    a2, 1
    wsr.windowstart a2
    rsync
    movi    a1, __stack_top
    movi    a0, 0

    /* .bss をゼロに(ROM ローダは .bss を運ばない) */
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0
1:  bgeu    a2, a3, 2f
    s32i    a4, a2, 0
    addi    a2, a2, 4
    j       1b
2:
    movi    a2, PS_UM | PS_WOE | 15
    wsr.ps  a2
    rsync
    call4   kmain
3:  j       3b
    .size   _start, . - _start

    .text

/* ---- 例外入口: レベル 1 割込み(タイマ)/ syscall / それ以外(障害) ---- */
    .align  4
exc_entry:
    addi    a1, a1, -CTX_SIZE
    s32i    a0, a1, CTX_A(0)
    s32i    a2, a1, CTX_A(2)
    s32i    a3, a1, CTX_A(3)
    s32i    a4, a1, CTX_A(4)
    s32i    a5, a1, CTX_A(5)
    s32i    a6, a1, CTX_A(6)
    s32i    a7, a1, CTX_A(7)
    s32i    a8, a1, CTX_A(8)
    s32i    a9, a1, CTX_A(9)
    s32i    a10, a1, CTX_A(10)
    s32i    a11, a1, CTX_A(11)
    s32i    a12, a1, CTX_A(12)
    s32i    a13, a1, CTX_A(13)
    s32i    a14, a1, CTX_A(14)
    s32i    a15, a1, CTX_A(15)
    rsr.epc1 a2
    s32i    a2, a1, CTX_PC
    rsr.ps  a2
    movi    a3, ~PS_EXCM
    and     a2, a2, a3
    s32i    a2, a1, CTX_PS
    rsr.sar a2
    s32i    a2, a1, CTX_SAR
    rsr.lbeg a2
    s32i    a2, a1, CTX_LBEG
    rsr.lend a2
    s32i    a2, a1, CTX_LEND
    rsr.lcount a2
    s32i    a2, a1, CTX_LCOUNT
    rsr.exccause a2
    s32i    a2, a1, CTX_CAUSE
    rsr.excvaddr a2
    s32i    a2, a1, CTX_VADDR

    /* ループを殺してから EXCM を落とす(LCOUNT が残っていると、ハンドラの
     * 番地がたまたま LEND に一致したとき LBEG へ飛ぶ)。 */
    movi    a2, 0
    wsr.lcount a2
    movi    a2, PS_UM | PS_WOE | 15
    wsr.ps  a2
    rsync

    /* 割り込まれた側の呼び出し元たちの窓を吐き出す。あふれの置き場所は
     * 「割り込まれた関数の SP」から決まるので、その間だけ a1 を元に戻す。 */
    addi    a1, a1, CTX_SIZE
    SPILL_ALL_WINDOWS
    addi    a1, a1, -CTX_SIZE

    l32i    a2, a1, CTX_CAUSE
    beqi    a2, 4, exc_irq              /* Level1Interrupt */
    beqi    a2, 1, exc_syscall          /* SyscallCause */

    /* それ以外は障害。報告して、外部コマンドならその枠を畳んで先へ進む
     * (slot0 = カーネルなら esp32_fault の中で止まる)。 */
    l32i    a6, a1, CTX_CAUSE
    mov     a7, a1
    l32i    a8, a1, CTX_VADDR
    call4   esp32_fault
    j       exc_exit

exc_irq:
    call4   esp32_irq                   /* タイマなら tick を進める(kmain.c) */
    mov     a6, a1
    call4   sched_tick_sp               /* src/kernel.c: 今の SP を控え、次の SP を返す */
    mov     a1, a6
    j       exc_restore

/* syscall: a2 = 番号、a3..a6 = 引数、戻り値 a2。
 *   0 = exit(今の文脈は捨てる)/ 0xFFFFFFFF = KYIELD(tick は進めず順番だけ回す)/
 *   それ以外は sys_call(src/sysfile.c。PLAT_FLAT32 のポート共通)。 */
exc_syscall:
    l32i    a2, a1, CTX_PC
    addi    a2, a2, 3                   /* syscall 命令(3 バイト)の次へ戻る */
    s32i    a2, a1, CTX_PC
    l32i    a2, a1, CTX_A(2)
    beqz    a2, exc_exit
    beqi    a2, -1, exc_yield
    mov     a6, a2
    l32i    a7, a1, CTX_A(3)
    l32i    a8, a1, CTX_A(4)
    l32i    a9, a1, CTX_A(5)
    l32i    a10, a1, CTX_A(6)
    call4   sys_call
    s32i    a6, a1, CTX_A(2)
    j       exc_restore

exc_yield:
    mov     a6, a1
    call4   sched_tick_sp
    mov     a1, a6
    j       exc_restore

exc_exit:
    l32i    a6, a1, CTX_A(3)            /* 終了コード(crt0cmd が a3 に載せる。#111) */
    call4   sched_exit_sp               /* 今の枠を空けて、次の SP を返す */
    mov     a1, a6

exc_restore:
    /* EXCM を立ててから(以後レベル 1 は入らない)ループレジスタと EPC1 を戻す。
     * rfe が EXCM を落とすので、戻った先の PS はフレームに控えた値になる。
     * 窓は「今の窓だけ生きている」にする(戻る先の呼び出し元は吐き出し済み)。 */
    l32i    a2, a1, CTX_PS
    movi    a3, PS_EXCM
    or      a2, a2, a3
    wsr.ps  a2
    rsync
    rsr.windowbase a2
    ssl     a2
    movi    a2, 1
    sll     a2, a2
    wsr.windowstart a2
    rsync
    l32i    a2, a1, CTX_LBEG
    wsr.lbeg a2
    l32i    a2, a1, CTX_LEND
    wsr.lend a2
    l32i    a2, a1, CTX_LCOUNT
    wsr.lcount a2
    l32i    a2, a1, CTX_SAR
    wsr.sar a2
    l32i    a2, a1, CTX_PC
    wsr.epc1 a2
    l32i    a0, a1, CTX_A(0)
    l32i    a2, a1, CTX_A(2)
    l32i    a3, a1, CTX_A(3)
    l32i    a4, a1, CTX_A(4)
    l32i    a5, a1, CTX_A(5)
    l32i    a6, a1, CTX_A(6)
    l32i    a7, a1, CTX_A(7)
    l32i    a8, a1, CTX_A(8)
    l32i    a9, a1, CTX_A(9)
    l32i    a10, a1, CTX_A(10)
    l32i    a11, a1, CTX_A(11)
    l32i    a12, a1, CTX_A(12)
    l32i    a13, a1, CTX_A(13)
    l32i    a14, a1, CTX_A(14)
    l32i    a15, a1, CTX_A(15)
    addi    a1, a1, CTX_SIZE
    rfe

/* ---- 来るはずのないベクタ(a2 = 由来)。専用スタックに替えて報告し、止まる ---- */
    .align  4
vec_fatal:
    wsr.excsave1 a2                     /* 由来。窓を作り直すと a2 が別のレジスタになるので退避 */
    movi    a3, PS_UM | 15
    wsr.ps  a3
    rsync
    movi    a3, 0
    wsr.windowbase a3
    rsync
    movi    a3, 1
    wsr.windowstart a3
    rsync
    movi    a1, exc_stk_top
    movi    a3, PS_UM | PS_WOE | 15
    wsr.ps  a3
    rsync
    rsr.excsave1 a6
    rsr.epc1 a7
    rsr.excvaddr a8
    rsr.exccause a9
    call4   esp32_panic
1:  j       1b

    .section .bss
    .align  16
exc_stk:
    .space  1024
exc_stk_top:
