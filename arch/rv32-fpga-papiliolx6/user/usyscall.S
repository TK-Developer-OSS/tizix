/* arch/rv32-fpga-papiliolx6/user/usyscall.S : unsigned long syscall5(func, a1, a2, a3, a4)
 *   Arguments follow the C calling convention, a0..a4. An ecall enters trap_entry (crt0.S). The result is in a0. */
	.text
	.globl	syscall5
syscall5:
	ecall
	ret
