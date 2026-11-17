/* arch/rv32-qemu/user/crt0cmd.S : the entry of external commands
 *   loader.c builds a fake context with a0=argc / a1=argv / sp=top / mepc=here, and mret comes here.
 *   Written with PC-relative code only (the load address differs from the link address; -mcmodel=medany -mno-relax). */
	.section .text.start
	.globl	_start
_start:
	mv	s0, a0
	mv	s1, a1
	la	t0, __bss_start
	la	t1, __bss_end
1:	bgeu	t0, t1, 2f
	sw	zero, 0(t0)
	addi	t0, t0, 4
	j	1b
2:	mv	a0, s0
	mv	a1, s1
	call	main
	/* exit: a0 = 0 (func), a1 = exit code (the return value of main) */
	mv	a1, a0
	li	a0, 0
	ecall
3:	j	3b
