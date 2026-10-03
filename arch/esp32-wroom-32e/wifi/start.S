/* WiFi 実験(#109 W1)用の起動。windowed ABI、ベクタは ROM のもの(VECBASE = 0x40000000 のまま)。
 * ROM ローダから来たら、窓を作り直し、自前のスタックへ移って main を呼ぶ。 */
    .section .text.entry, "ax"
    .literal_position
    .align 4
    .global _start
_start:
    movi    a2, 0
    wsr.windowbase a2
    rsync
    movi    a2, 1
    wsr.windowstart a2
    rsync
    movi    a1, _stack_top
    movi    a0, 0
    /* PS: WOE=1(bit18)、UM=1(bit5)、INTLEVEL=0。EXCM=0 */
    movi    a2, (1 << 18) | (1 << 5)
    wsr.ps  a2
    rsync
    /* .bss をゼロに */
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0
1:  bgeu    a2, a3, 2f
    s32i    a4, a2, 0
    addi    a2, a2, 4
    j       1b
2:  call4   main
3:  j       3b

/* 協調スレッドの切り替え(windowed)。
 *   void ctx_switch(uint32_t *save, const uint32_t *load)
 *   save[0] = 自分の SP、save[1] = 自分の a0(戻り先)。load から相手のぶんを読む。
 *   先に全部の窓をスタックへ吐き出す(SPILL)。そうすると呼び出し元の a0-a3 は
 *   「この関数の SP の 16 バイト下」にあり、retw は窓あふれ戻し(underflow)で
 *   それを読み戻す。相手側も同じ形で止まっているので、SP と a0 を差し替えて retw すれば
 *   相手の ctx_switch の呼び出し元へ戻る。 */
    .section .text, "ax"
    .literal_position
    .align 4
    .global ctx_switch
ctx_switch:
    entry   a1, 32
    rsil    a4, 3               /* 差し替えの途中で割込みに入られないように */
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
    s32i    a1, a2, 0
    s32i    a0, a2, 4
    l32i    a1, a3, 0
    l32i    a0, a3, 4
    wsr.ps  a4
    rsync
    retw

/* 新しいスレッドの入口。ctx_switch の retw がここへ「戻って」来る(call4 で呼ばれた
 * 関数の途中に戻った形)。a1 = スレッドのスタック、偽の呼び出し元の a0-a3 は underflow が
 * 読み戻した値(a2 = 関数、a3 = 引数を仕込んである)。 */
    .align 4
    .global thread_trampoline
thread_trampoline:
    mov     a6, a3
    callx4  a2
    call4   thread_exit
4:  j       4b
