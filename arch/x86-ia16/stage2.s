/* tizix x86-ia16 : stage2 = 本格ブートストラップ(gap 領域 LBA 1..8)
 *   stage1(VBR)が 0x0800:0000 にロードして far jump してくる。DL=起動ドライブ。
 *
 *   役割:
 *     - バナー表示(INT 10h teletype。stage2 が走った証拠)
 *     - kernel blob を CHS ループで 0x1000:0000 へロード
 *     - kernel へ far jump
 *   将来(M4): ここに FAT12 ルート走査を足して KERNEL.BIN をファイルとしてロード。
 *             512B 制限が無いので拡張はここに集約する。
 *
 *   1.44MB フロッピー幾何: 18 sect/track, 2 head。
 *     track = LBA/18 ; sector = LBA%18 + 1 ; head = track&1 ; cyl = track>>1
 */
	.code16
	.intel_syntax noprefix
	.section .text
	.global _s2

	.include "_layout.inc"   /* KERNEL_SECS(Makefile が kernel.bin から生成) */

KSEG       = 0x1000
KERNEL_LBA = 9          /* = STAGE2_LBA(1) + STAGE2_SECS(8) */
COM1       = 0x3F8      /* 16550: 出力は INT 10h でなく COM1 直叩き
                         * (QEMU では INT 10h は VGA に出て -serial に来ない) */

_s2:
	cli
	mov  ax, cs
	mov  ds, ax             /* DS = stage2 セグメント(メッセージ参照用) */
	xor  ax, ax
	mov  es, ax
	mov  ss, ax
	mov  sp, 0x7c00
	cld
	mov  [drive], dl

	call uart_init
	mov  si, offset msg_banner
	call puts

	/* --- kernel blob を CHS ループで KSEG:0 へ --- */
	mov  ax, KSEG
	mov  es, ax
	mov  word ptr [bufoff], 0
	mov  word ptr [lba], KERNEL_LBA
	mov  di, KERNEL_SECS

next_sect:
	mov  ax, [lba]
	xor  dx, dx
	div  word ptr [spt]     /* ax = track, dx = sector-1 */
	mov  cl, dl
	inc  cl                 /* CL = sector (1..18) */
	shr  ax, 1              /* ax = cylinder, CF = head bit */
	mov  ch, al             /* CH = cylinder low 8 */
	mov  dh, 0
	adc  dh, 0              /* DH = head (0/1) */
	mov  dl, [drive]
	mov  bx, [bufoff]
	mov  ax, 0x0201         /* AH=02 read, AL=01 */
	int  0x13
	jc   err

	add  word ptr [bufoff], 512
	inc  word ptr [lba]
	dec  di
	jnz  next_sect

	mov  si, offset msg_go
	call puts

	ljmp KSEG, 0x0000

err:
	mov  si, offset msg_err
	call puts
1:	hlt
	jmp  1b

/* 16550 を 115200 8N1 に(QEMU 用の最小 init) */
uart_init:
	push ax
	push dx
	mov  dx, COM1 + 3
	mov  al, 0x80
	out  dx, al            /* DLAB=1 */
	mov  dx, COM1 + 0
	mov  al, 0x01
	out  dx, al            /* divisor lo = 1 */
	mov  dx, COM1 + 1
	xor  al, al
	out  dx, al            /* divisor hi = 0 */
	mov  dx, COM1 + 3
	mov  al, 0x03
	out  dx, al            /* 8N1, DLAB=0 */
	mov  dx, COM1 + 4
	mov  al, 0x0B
	out  dx, al            /* DTR|RTS|OUT2 */
	pop  dx
	pop  ax
	ret

/* SI=asciz を COM1 へ(THRE 待ちポーリング) */
puts:
	push ax
	push dx
2:	lodsb
	test al, al
	jz   3f
	mov  ah, al
4:	mov  dx, COM1 + 5
	in   al, dx
	test al, 0x20         /* LSR THRE */
	jz   4b
	mov  al, ah
	mov  dx, COM1
	out  dx, al
	jmp  2b
3:	pop  dx
	pop  ax
	ret

drive:  .byte 0
lba:    .word 0
bufoff: .word 0
spt:    .word 18
msg_banner: .asciz "tizix stage2\r\n"
msg_go:     .asciz "stage2: entering kernel\r\n"
msg_err:    .asciz "stage2: kernel load error\r\n"
