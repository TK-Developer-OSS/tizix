/* arch/m68k-mega/user/usyscall.s : コマンド側 syscall スタブ
 *   unsigned long syscall5(unsigned long func, a1, a2, a3, a4);
 *   D0=func D1=a1 D2=a2 D3=a3 D4=a4 で TRAP #0。戻り値は D0(そのまま返す)。
 *
 *   #47 バグ修正: m68k SysV ABI では D2-D7/A2-A6 は callee-saved。
 *   旧実装は呼び出し元の D2(a2 の受け皿)を保存せずそのまま上書きしていた
 *   ため、呼び出し元(GCC が D2 にレジスタ割付けしたローカル変数、例えば
 *   ls.c の lflag)を syscall5 を呼ぶたびに破壊していた ── `ls /` で
 *   readdir() 呼び出し後に lflag が意図せず真になり、無関係な
 *   readdir_size() 分岐へ迷い込んで暴走(起動直後に戻ったように見える)する
 *   原因だった。D2-D4 を保存・復帰してから使う。 */
    .globl  syscall5
syscall5:
    movem.l %d2-%d4, -(%sp)
    move.l  16(%sp), %d0
    move.l  20(%sp), %d1
    move.l  24(%sp), %d2
    move.l  28(%sp), %d3
    move.l  32(%sp), %d4
    trap    #0
    movem.l (%sp)+, %d2-%d4
    rts
