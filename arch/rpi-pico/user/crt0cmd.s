/* arch/rpi-pico/user/crt0cmd.s : ARM Cortex-M0+ 外部コマンドの C ランタイム入口 */
    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .globl _start
_start:
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1

    /* .bss 領域のゼロクリア */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    eors r2, r2          /* r2 = 0 (Cortex-M0+ 互換の XOR 即値ゼロクリア) */
1:
    cmp r0, r1
    bge 2f
    strb r2, [r0]
    adds r0, r0, #1
    b 1b
2:

    /* main(argc, argv) 呼び出し */
    mov r0, r4
    mov r1, r5
    bl main

    /* 終了処理 (exit syscall: func = 0, r1 = main の戻り値) */
    mov r1, r0
    eors r0, r0          /* r0 = 0 */
    svc #0

3:
    b 3b

