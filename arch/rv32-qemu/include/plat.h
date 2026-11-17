#ifndef _PLAT_H
#define _PLAT_H
/* ============================================================================
 * arch/rv32-qemu board map (QEMU riscv32 virt, -bios none; M mode only)
 *
 *   0x02000000 CLINT        mtimecmp = +0x4000(64bit)、mtime = +0xBFF8(64bit、10MHz)
 *   0x10000000 NS16550 UART registers are one byte wide. +0=RBR/THR, +5=LSR (bit0=data ready, bit5=transmitter empty)
 *   0x80000000 RAM(128MB)
 *       0x80000000 - 0x800FFFFF  the kernel (code, data, bss). The stack grows down from 0x80100000
 *       0x80100000 -             process slots (PLAT_SLOT_ADDR), 64KB each
 *       0x82000000 -             the disk image (placed with QEMU's -device loader; diskio.c)
 *
 *   This is for QEMU's virt machine, not the memory map of the board's SoC. The difference is in the
 *   Keep the platform layer (console.c / diskio.c / timer) self-contained.
 * ========================================================================= */

#define RAM_ORIGIN       0x80000000UL
#define KERNEL_STACK_TOP 0x80100000UL

#define UART_BASE        0x10000000UL
#define CLINT_BASE       0x02000000UL
#define CLINT_MTIMECMP   (CLINT_BASE + 0x4000UL)
#define CLINT_MTIME      (CLINT_BASE + 0xBFF8UL)
#define MTIME_HZ         10000000UL

#define DISK_BASE        0x82000000UL
#define DISK_BYTES       (16UL * 1024 * 1024)

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
