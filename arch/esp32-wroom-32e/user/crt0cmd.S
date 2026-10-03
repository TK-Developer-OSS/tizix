/* arch/esp32-wroom-32e/user/crt0cmd.S : 外部コマンドの入口と syscall の出口
 *
 *   kexec_argv(src/kexec.c + loader.c の plat_ctx)が偽コンテキストで a2 = argc /
 *   a3 = &argv[0] / a1 = データ枠の頂上 を載せて _start へ「復帰」してくる。call0 ABI の
 *   第 1・第 2 引数がそのまま a2 / a3 なので、何もせず main を呼べる。.bss のゼロ埋めは
 *   loader.c、argv[] の用意は src/kexec.c が済ませている。
 *
 *   syscall: a2 = 番号、a3..a6 = 引数、戻り値 a2(カーネル側は crt0.S exc_syscall)。
 *   これも call0 の引数レジスタの並びそのままなので、syscall5 は命令 1 個。
 *   番号 0 = exit。
 */
    .section .text.start, "ax"
    .global _start
    .type   _start, @function
    .align  4
_start:
    call0   main
    mov     a3, a2                      /* 終了コード = main の戻り値(#111) */
    movi    a2, 0
    syscall
1:  j       1b
    .size   _start, . - _start

    .text
    .global syscall5
    .type   syscall5, @function
    .align  4
/* unsigned long syscall5(unsigned long func, a1, a2, a3, a4); */
syscall5:
    syscall
    ret
    .size   syscall5, . - syscall5
