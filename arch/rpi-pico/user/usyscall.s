/* arch/rpi-pico/user/usyscall.s : ARM Cortex-M0+ コマンド側 syscall スタブ */
    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .globl syscall5
syscall5:
    /* r0=func, r1=a1, r2=a2, r3=a3, stack=a4 */
    svc #0
    bx lr
