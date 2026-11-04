#ifndef _PLAT_H
#define _PLAT_H
/* ============================================================================
 * arch/m68k-x68000 board map (Sharp X68000, MC68000, XM6G / hardware)
 *
 * ROM IPL -> boot_ipl.s (the floppy's boot sector) -> loads TIZIX.SYS at $10000.
 * The console and the disk go through the ROM's IOCS (TRAP #15).
 *
 *   $000000-$00FFFF  ROM vector table, IOCS work area, Human68k area (not touched)
 *   $010000-$018FFF  slot 0 = kernel (image + bss, 36KB)
 *   $019000-$0F8FFF  slots 1.. = external commands (32KB each)
 *   $0F9000-$0FEFFF  kernel stack (grows down from $0FF000)
 *   $0FF000-$0FFFFF  console output ring (for the regression test, console.c)
 * ========================================================================= */

#define RAM_ORIGIN      0x010000UL
#define RAM_END         0x100000UL   /* a 1MB machine is the minimum */
#define STACK_TOP       0x0FF000UL   /* top of the kernel stack (same as link-kernel.ld) */
#define CONLOG_ADDR     0x0FF000UL   /* console output ring (4KB) */

/* Start of slot n (1..). The kernel (slot 0) must fit in KERNEL_AREA (checked by the Makefile) */
#define KERNEL_AREA       0x9000UL
#define PLAT_FLAT32       1
#define PLAT_NSLOT        29
#define PLAT_SLOT_SIZE    0x8000UL
#define PLAT_SLOT_ADDR(n) (RAM_ORIGIN + KERNEL_AREA - 0x8000UL + (unsigned long)(n) * 0x8000UL)
#define PROC_SLOT_KB      32

#define IRQ_OFF()  __asm__ volatile ("ori.w  #0x0700,%%sr" ::: "memory")
#define IRQ_ON()   __asm__ volatile ("andi.w #0xf8ff,%%sr" ::: "memory")
#define KYIELD()   __asm__ volatile ("trap #1" ::: "memory")

#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

#endif
