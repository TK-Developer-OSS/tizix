/* entry of the boot loader. It starts at address 0 of IMEM (ROM). The stack is in DMEM (0xA0000000, 4KB). */
	.section .text.start
	.globl	_start
_start:
	li	sp, 0xA0001000
	call	main
1:	j	1b
