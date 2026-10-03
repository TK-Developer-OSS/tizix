/* arch/esp32-wroom-32e/net/ctx.S(#109): slot0 の中の協調スレッドの切り替え(windowed)。
 *
 *   void ctx_switch(uint32_t *save, const uint32_t *load)
 *     save[0] = 自分の SP、save[1] = 自分の a0(戻り先)。load から相手のぶんを読む。
 *     先に全部の窓をスタックへ吐き出す。そうすると呼び出し元の a0-a3 は「この関数の SP の
 *     16 バイト下」にあり、retw は窓の戻し(underflow)でそれを読み戻す。相手側も同じ形で
 *     止まっているので、SP と a0 を差し替えて retw すれば相手の ctx_switch の呼び出し元へ戻る。
 *   thread_trampoline: 新しいスレッドの入口(osal.c が偽の 16 バイトを仕込む)。 */
    .text
    .literal_position
    .align 4
    .global ctx_switch
ctx_switch:
    entry   a1, 32
    rsil    a4, 3                       /* 差し替えの途中で割込みに入られないように */
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

    .align 4
    .global thread_trampoline
thread_trampoline:
    mov     a6, a3
    callx4  a2
    call4   thread_exit
1:  j       1b
