/* arch/rv32-fpga-papiliolx6 crt0.S - start-up and the M-mode trap entry
 *
 *   The context layout = 144 bytes (CTX_SIZE; must match plat_ctx in loader.c):
 *     +0..+120   x1, x2(sp), x3 ... x31 (x(i) is at (i-1)*4)
 *     +124 mepc  +128 mstatus  +132..+143 unused (keeps sp on a 16-byte boundary)
 *   On every trap, push onto the stack of the process (or the kernel) and take that SP,
 *   pass it to sched_tick_sp (src/kernel.c), receive "the SP to run next", swap it in and mret.
 *
 *   ecall convention: a0 = func (0 = exit, -1 = KYIELD, anything else goes to sys_call), a1..a4 = arguments. The result is in a0.
 *   Only M mode is used (processes run in M mode too, like the supervisor behaviour of the 68000).
 */
	.set	CTX_SIZE, 144

	.section .text.start
	.globl	_start
_start:
	csrr	t0, mhartid
	bnez	t0, park
	la	sp, __stack_top
	la	t0, trap_entry
	csrw	mtvec, t0
	la	t0, _bss_start
	la	t1, _bss_end
1:	bgeu	t0, t1, 2f
	sw	zero, 0(t0)
	addi	t0, t0, 4
	j	1b
2:	call	kmain
park:	wfi
	j	park

	.macro	SAVE_REGS
	sw	x1,    0(sp)
	sw	x3,    8(sp)
	sw	x4,   12(sp)
	sw	x5,   16(sp)
	sw	x6,   20(sp)
	sw	x7,   24(sp)
	sw	x8,   28(sp)
	sw	x9,   32(sp)
	sw	x10,  36(sp)
	sw	x11,  40(sp)
	sw	x12,  44(sp)
	sw	x13,  48(sp)
	sw	x14,  52(sp)
	sw	x15,  56(sp)
	sw	x16,  60(sp)
	sw	x17,  64(sp)
	sw	x18,  68(sp)
	sw	x19,  72(sp)
	sw	x20,  76(sp)
	sw	x21,  80(sp)
	sw	x22,  84(sp)
	sw	x23,  88(sp)
	sw	x24,  92(sp)
	sw	x25,  96(sp)
	sw	x26, 100(sp)
	sw	x27, 104(sp)
	sw	x28, 108(sp)
	sw	x29, 112(sp)
	sw	x30, 116(sp)
	sw	x31, 120(sp)
	.endm

	.align	4
	.globl	trap_entry
trap_entry:
	addi	sp, sp, -CTX_SIZE
	SAVE_REGS
	addi	t0, sp, CTX_SIZE
	sw	t0, 4(sp)			/* sp of the interrupted side */
	csrr	t0, mepc
	sw	t0, 124(sp)
	csrr	t0, mstatus
	sw	t0, 128(sp)
	csrr	t0, mcause
	bltz	t0, intr
	li	t1, 11
	bne	t0, t1, fault

	/* ---- ecall (M mode) ---- */
	lw	t0, 124(sp)
	addi	t0, t0, 4			/* return to the instruction after the ecall */
	sw	t0, 124(sp)
	li	t1, -1
	beq	a0, t1, do_yield
	beqz	a0, do_exit
	call	sys_call			/* a0..a4 = func, a1..a4 */
	sw	a0, 36(sp)
	j	restore

do_exit:
	mv	a0, a1				/* exit code */
	call	sched_exit_sp
	mv	sp, a0
	j	restore

do_yield:
	mv	a0, sp
	call	sched_tick_sp
	mv	sp, a0
	j	restore

	/* ---- interrupts: machine timer (7) and UART0 receive (FIRQ2 = 18) ---- */
intr:
	slli	t0, t0, 1
	srli	t0, t0, 1			/* drop the top bit */
	li	t1, 18
	beq	t0, t1, uart_rx_intr
	li	t1, 7
	bne	t0, t1, fault
	call	timer_rearm
	call	plt_interrupt
	mv	a0, sp
	call	sched_tick_sp
	mv	sp, a0
	j	restore

	/* UART receive: just move the FIFO into the receive buffer. Do not reschedule; return straight to the interrupted context */
uart_rx_intr:
	call	con_rx_irq
	j	restore

	/* ---- exceptions: report and stop (the stack may be broken, so switch to a dedicated one) ---- */
fault:
	csrr	a0, mcause
	csrr	a1, mepc
	csrr	a2, mtval
	mv	a3, ra
	mv	a4, sp
	la	sp, exc_stk_top
	call	exc_report
1:	wfi
	j	1b

restore:
	/* C code (IRQ_ON in sys_call and so on) may return with MIE set. An interrupt in the middle of the return
	 * it overwrites mepc, so it must be closed here (mret restores the state from MPIE). */
	csrci	mstatus, 8
	lw	t0, 124(sp)
	csrw	mepc, t0
	lw	t0, 128(sp)
	csrw	mstatus, t0
	lw	x1,    0(sp)
	lw	x3,    8(sp)
	lw	x4,   12(sp)
	lw	x5,   16(sp)
	lw	x6,   20(sp)
	lw	x7,   24(sp)
	lw	x8,   28(sp)
	lw	x9,   32(sp)
	lw	x10,  36(sp)
	lw	x11,  40(sp)
	lw	x12,  44(sp)
	lw	x13,  48(sp)
	lw	x14,  52(sp)
	lw	x15,  56(sp)
	lw	x16,  60(sp)
	lw	x17,  64(sp)
	lw	x18,  68(sp)
	lw	x19,  72(sp)
	lw	x20,  76(sp)
	lw	x21,  80(sp)
	lw	x22,  84(sp)
	lw	x23,  88(sp)
	lw	x24,  92(sp)
	lw	x25,  96(sp)
	lw	x26, 100(sp)
	lw	x27, 104(sp)
	lw	x28, 108(sp)
	lw	x29, 112(sp)
	lw	x30, 116(sp)
	lw	x31, 120(sp)
	lw	x2,    4(sp)
	mret

	.section .bss
	.align	4
exc_stk:
	.space	512
exc_stk_top:
