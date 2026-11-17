#ifndef _PLAT_H
#define _PLAT_H
/* ============================================================================
 * arch/rv32-fpga-papiliolx6 board map (NEORV32 + RAM behind the external bus (XBUS); M mode only)
 *
 *   0xFFF40000 CLINT  mtimecmp = +0x4000 (64 bit), mtime = +0xBFF8 (64 bit, counts at the clock rate)
 *   0xFFF50000 UART0  +0=CTRL, +4=BAUD, +8=DATA (low 8 bits). CTRL bit8=RX empty, bit14=TX FIFO full
 *   0x80000000 RAM (XBUS. The tb's behavioural RAM in simulation, the SDRAM controller on the board)
 *       0x80000000 - 0x800FFFFF  the kernel (code, data, bss). The stack grows down from 0x80100000
 *       0x80100000 - 0x801FFFFF  process slots (PLAT_SLOT_ADDR), 64KB each
 *       0x80200000 -             the disk image (a RAM disk; diskio.c)
 *
 *   Compared with rv32-qemu, only the CLINT/UART addresses, the UART registers and the amount of memory differ.
 *   Confine the differences to console.c / kmain.c / this plat.h.
 * ========================================================================= */

/* the simulation default is 10MHz / 115200 baud (the same values as tb.v). On the board, make passes CLK_HZ */
#ifndef CLK_HZ
#define CLK_HZ           10000000UL
#endif
#ifndef UART_BAUD
#define UART_BAUD        115200UL
#endif

#define RAM_ORIGIN       0x80000000UL
#define KERNEL_STACK_TOP 0x80100000UL

#define UART_BASE        0xFFF50000UL
#define CLINT_BASE       0xFFF40000UL
#define CLINT_MTIMECMP   (CLINT_BASE + 0x4000UL)
#define CLINT_MTIME      (CLINT_BASE + 0xBFF8UL)
#define MTIME_HZ         CLK_HZ

#define DISK_BASE        0x80200000UL
#define DISK_BYTES       (4UL * 1024 * 1024)

#define PLAT_FLAT32       1
#define PLAT_NSLOT        16
#define PLAT_SLOT_SIZE    0x10000UL
#define PLAT_SLOT_ADDR(n) (0x80100000UL + (unsigned long)(n) * 0x10000UL)
#define PROC_SLOT_KB      64

/* interrupts are disabled/enabled with mstatus.MIE (bit3). */
#define IRQ_OFF()  __asm__ volatile ("csrci mstatus, 8" ::: "memory")
#define IRQ_ON()   __asm__ volatile ("csrsi mstatus, 8" ::: "memory")
/* give up the CPU voluntarily: ecall with func = -1 (trap_entry in crt0.S). The tick does not advance. */
#define KYIELD()   __asm__ volatile ("li a0, -1\n\tecall" ::: "a0", "memory")

/* a porting shim to get the shared src/ through gcc (removes the SDCC-only attributes) */
#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

#endif
