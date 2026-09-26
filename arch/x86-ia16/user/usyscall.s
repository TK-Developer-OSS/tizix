/* arch/x86-ia16/user/usyscall.s : コマンド側 syscall スタブ
 *   unsigned syscall5(unsigned func, unsigned al, unsigned bx,
 *                     unsigned cx, unsigned si, unsigned di);
 *   AH=func(下位バイト), AL=al(下位バイト), BX/CX/SI/DI をセットして INT 80h。戻り AX。
 *   [bp+4]=func [bp+6]=al [bp+8]=bx [bp+10]=cx [bp+12]=si [bp+14]=di
 */
	.code16
	.intel_syntax noprefix
	.text
	.global syscall5

syscall5:
	push bp
	mov  bp, sp
	push si
	push di
	push bx
	mov  ah, [bp+4]       /* AH = func の下位バイト */
	mov  al, [bp+6]       /* AL = al   の下位バイト */
	mov  bx, [bp+8]
	mov  cx, [bp+10]
	mov  si, [bp+12]
	mov  di, [bp+14]
	int  0x80
	pop  bx
	pop  di
	pop  si
	pop  bp
	ret
