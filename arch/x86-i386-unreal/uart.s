/* arch/x86-i386-unreal/uart.s  --  16550 シリアルの割り込み受信ドライバ(COM1 = 0 番、COM2 = 1 番)
 *   受信は IRQ4(COM1、INT 0Ch)/ IRQ3(COM2、INT 0Bh)で、ISR がポートごとのリング(512B)へ貯める。
 *   以前はコンソール(COM1)もネットの下回り(COM2、ユーザー側の netmodem16 / netcli16)もポートを直接ポーリングして
 *   いたので、待つと 16 バイトの FIFO(115200bps で約 1.4ms)が溢れ、待たないと CPU を使い切っていた。
 *   ここで貯めておけば、読む側は x86_kyield で譲るか hlt で寝ていてよい(受信の割り込みで起きる)。
 *   送信はポーリングのまま(THRE を待って 1 バイト)。
 *
 *   C から(cdecl、DS = カーネル):
 *     unsigned uart_hw_open(unsigned n, unsigned div, unsigned mcr)  初期化して受信割り込みを有効にする(1。16550 が無ければ 0)。mcr の bit0/1 = DTR/RTS
 *                                                             (OUT2 = bit3 は IRQ を PIC へ通すのでここで足す)
 *     unsigned uart_avail(unsigned n)                         リングに溜まっているバイト数
 *     int  uart_getb(unsigned n)                              1 バイト取り出す(無ければ -1)
 *     void uart_putb(unsigned n, unsigned c)                  1 バイト送る
 *     unsigned uart_msr(unsigned n)                           モデム状態レジスタ(bit7 = DCD)
 *     unsigned uart_present(unsigned n)                       16550 があるか(スクラッチレジスタの読み戻し)
 *   ISR は割り込まれた側の SS を使い、DS だけカーネルに切り替える(積むのは 12 バイト)。
 */
	.code16
	.intel_syntax noprefix

KSEG    = 0x1000
RBSZ    = 512                       /* リングの大きさ(2 のべき乗)。115200bps で約 44ms 分 */

	.text
	.global uart_hw_open
	.global uart_avail
	.global uart_getb
	.global uart_putb
	.global uart_msr
	.global uart_present

/* ポート番号 n(0/1)の I/O 番地 -> dx */
uart_base:
	mov  dx, 0x3F8
	test bx, bx
	jz   1f
	mov  dx, 0x2F8
1:	ret

/* ---- ISR(n = 0: COM1 / 1: COM2) ---- */
_isr_com1:
	push bx
	xor  bx, bx
	jmp  isr_com
_isr_com2:
	push bx
	mov  bx, 1
isr_com:
	push ax
	push cx
	push dx
	push si
	push ds
	mov  ax, KSEG
	mov  ds, ax
	call uart_base
	mov  si, bx
	shl  si, 1                      /* si = n * 2(head / tail の添字) */
	mov  ah, 64                     /* 1 回の割り込みで読む上限(FIFO は 16。止まらない装置への保険) */
1:	add  dx, 5                      /* LSR */
	in   al, dx
	sub  dx, 5
	cmp  al, 0xFF                   /* 装置が無い(浮いたバス) */
	je   4f
	test al, 1                      /* データあり? */
	jz   4f
	dec  ah
	jz   4f
	in   al, dx                     /* RBR */
	mov  bx, [rb_head + si]
	mov  cx, bx
	inc  cx
	and  cx, RBSZ - 1
	cmp  cx, [rb_tail + si]
	je   1b                         /* 満杯: 捨てる */
	test si, si
	jnz  2f
	mov  [rb0 + bx], al
	jmp  3f
2:	mov  [rb1 + bx], al
3:	mov  [rb_head + si], cx
	jmp  1b
4:	mov  al, 0x20
	out  0x20, al                   /* PIC1 EOI(IRQ3 / IRQ4 は PIC1) */
	pop  ds
	pop  si
	pop  dx
	pop  cx
	pop  ax
	pop  bx
	iret

/* unsigned uart_present(unsigned n): スクラッチレジスタ(+7)に書いて読み戻す */
uart_present:
	push bp
	mov  bp, sp
	push bx
	mov  bx, [bp+4]
	call uart_base
	add  dx, 7
	mov  al, 0x5A
	out  dx, al
	in   al, dx
	cmp  al, 0x5A
	jne  1f
	mov  al, 0xA5
	out  dx, al
	in   al, dx
	cmp  al, 0xA5
	jne  1f
	mov  ax, 1
	jmp  2f
1:	xor  ax, ax
2:	pop  bx
	pop  bp
	ret

/* unsigned uart_hw_open(unsigned n, unsigned div, unsigned mcr): 1 = 開いた / 0 = 16550 が無い(何もしない) */
uart_hw_open:
	push bp
	mov  bp, sp
	push word ptr [bp+4]
	call uart_present
	add  sp, 2
	test ax, ax
	jnz  0f
	pop  bp
	ret
0:	push bx
	push cx
	push es
	pushf
	cli
	mov  bx, [bp+4]
	/* IVT: COM1 -> INT 0Ch(0x30)、COM2 -> INT 0Bh(0x2C) */
	xor  ax, ax
	mov  es, ax
	test bx, bx
	jnz  1f
	mov  word ptr es:0x30, offset _isr_com1
	mov  word ptr es:0x32, cs
	jmp  2f
1:	mov  word ptr es:0x2C, offset _isr_com2
	mov  word ptr es:0x2E, cs
2:	call uart_base
	inc  dx                         /* IER = 0 */
	xor  al, al
	out  dx, al
	add  dx, 2                      /* LCR = DLAB */
	mov  al, 0x80
	out  dx, al
	sub  dx, 3                      /* DLL / DLM */
	mov  ax, [bp+6]
	out  dx, al
	inc  dx
	mov  al, ah
	out  dx, al
	add  dx, 2                      /* LCR = 8N1 */
	mov  al, 0x03
	out  dx, al
	dec  dx                         /* FCR: FIFO 有効・クリア・受信の閾値 1 バイト */
	mov  al, 0x07
	out  dx, al
	add  dx, 2                      /* MCR = 呼び出し側の DTR/RTS + OUT2 */
	mov  al, [bp+8]
	and  al, 0x03
	or   al, 0x08
	out  dx, al
	sub  dx, 4                      /* 溜まっているものを捨てる(上限つき) */
	mov  cx, 64
3:	add  dx, 5
	in   al, dx
	sub  dx, 5
	test al, 1
	jz   4f
	in   al, dx
	loop 3b
4:	shl  bx, 1
	mov  word ptr [rb_head + bx], 0
	mov  word ptr [rb_tail + bx], 0
	shr  bx, 1
	inc  dx                         /* IER = 受信データあり */
	mov  al, 0x01
	out  dx, al
	in   al, 0x21                   /* PIC1: IRQ4(COM1)/ IRQ3(COM2)を通す */
	test bx, bx
	jnz  5f
	and  al, 0xEF
	jmp  6f
5:	and  al, 0xF7
6:	out  0x21, al
	popf
	pop  es
	pop  cx
	pop  bx
	mov  ax, 1
	pop  bp
	ret

/* unsigned uart_avail(unsigned n) */
uart_avail:
	push bp
	mov  bp, sp
	push bx
	mov  bx, [bp+4]
	shl  bx, 1
	pushf
	cli
	mov  ax, [rb_head + bx]
	sub  ax, [rb_tail + bx]
	popf
	and  ax, RBSZ - 1
	pop  bx
	pop  bp
	ret

/* int uart_getb(unsigned n) */
uart_getb:
	push bp
	mov  bp, sp
	push bx
	push si
	mov  si, [bp+4]
	shl  si, 1
	pushf
	cli
	mov  bx, [rb_tail + si]
	cmp  bx, [rb_head + si]
	jne  1f
	popf
	mov  ax, -1
	jmp  3f
1:	xor  ax, ax
	test si, si
	jnz  2f
	mov  al, [rb0 + bx]
	jmp  4f
2:	mov  al, [rb1 + bx]
4:	inc  bx
	and  bx, RBSZ - 1
	mov  [rb_tail + si], bx
	popf
3:	pop  si
	pop  bx
	pop  bp
	ret

/* void uart_putb(unsigned n, unsigned c): THRE を待って(上限つき)送る */
uart_putb:
	push bp
	mov  bp, sp
	push bx
	push cx
	mov  bx, [bp+4]
	call uart_base
	add  dx, 5
	mov  cx, 0xFFFF
1:	in   al, dx
	test al, 0x20
	jnz  2f
	loop 1b
2:	sub  dx, 5
	mov  al, [bp+6]
	out  dx, al
	pop  cx
	pop  bx
	pop  bp
	ret

/* unsigned uart_msr(unsigned n) */
uart_msr:
	push bp
	mov  bp, sp
	push bx
	mov  bx, [bp+4]
	call uart_base
	add  dx, 6
	xor  ax, ax
	in   al, dx
	pop  bx
	pop  bp
	ret

	.bss
rb_head: .space 4
rb_tail: .space 4
rb0:     .space RBSZ
rb1:     .space RBSZ
