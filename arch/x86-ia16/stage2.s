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

	/* --- メモリ情報を 0000:0512.. へ(カーネルの meminfo syscall が読む)---
	 *   0512: 従来メモリ KB(INT 12h)   0514: 拡張メモリ KB(INT 15h AH=88h、1MB 超から最大 64MB 弱)
	 *   0516: E801 の 1〜16MB を KB     0518: E801 の 16MB 超を 64KB 単位 */
	xor  ax, ax
	mov  es, ax
	int  0x12
	mov  es:[0x512], ax
	mov  word ptr es:[0x514], 0
	mov  word ptr es:[0x516], 0
	mov  word ptr es:[0x518], 0
	mov  ah, 0x88
	int  0x15
	jc   1f
	mov  es:[0x514], ax
1:	mov  ax, 0xE801
	int  0x15
	jc   2f
	mov  es:[0x516], ax
	mov  es:[0x518], bx
2:

	/* --- kernel blob を CHS ループで KSEG:0 へ --- */
	mov  ax, KSEG
	mov  es, ax
	mov  word ptr [bufoff], 0
	mov  word ptr [lba], KERNEL_LBA
	mov  di, KERNEL_SECS
	mov  byte ptr [tries], 4   /* 読込みリトライ回数(実機の FDD は一過性エラーがある) */

next_sect:
	cmp  byte ptr [drive], 0x80
	jb   chs_sect
	/* HDD 系(0x80 以降): INT 13h 拡張で 1 セクタずつ KSEG:bufoff へ */
	mov  ax, [bufoff]
	mov  [dap_off], ax
	mov  ax, [lba]
	mov  [dap_lba], ax
	mov  si, offset dap
	mov  ah, 0x42
	mov  dl, [drive]
	int  0x13
	jmp  rd_chk
chs_sect:
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
rd_chk:
	jnc  rd_ok
	xor  ah, ah             /* エラー: ディスクをリセットして同じセクタを再試行 */
	mov  dl, [drive]
	int  0x13
	dec  byte ptr [tries]
	jnz  next_sect
	jmp  err
rd_ok:
	mov  byte ptr [tries], 4

	add  word ptr [bufoff], 512
	inc  word ptr [lba]
	dec  di
	jnz  next_sect

	mov  si, offset msg_go
	call puts

	/* 起動ドライブ番号を 0000:0510 に置く(カーネルの diskio が読む。以前は 0x00 固定) */
	xor  ax, ax
	mov  es, ax
	mov  al, [drive]
	mov  byte ptr es:[0x510], al

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

/* SI=asciz を画面(BIOS INT 10h)と COM1 の両方へ。
 *   実機(ThinkPad R40 等)にはシリアルポートが無いので、COM1 の THRE 待ちは
 *   上限付き(無限ループにしない)。LSR が 0xFF(バスが浮いている=UART 無し)なら即スキップ。 */
puts:
	push ax
	push bx
	push cx
	push dx
2:	lodsb
	test al, al
	jz   3f
	push ax                 /* 文字を退避(INT 10h が AX を壊してもよいように) */
	mov  ah, 0x0E           /* BIOS teletype 出力 */
	mov  bx, 0x0007
	int  0x10
	pop  ax
	mov  ah, al             /* AH = 文字 */
	mov  dx, COM1 + 5
	mov  cx, 0x2000         /* 待ちの上限 */
4:	in   al, dx
	cmp  al, 0xFF           /* UART 無し(浮いたバス) */
	je   2b
	test al, 0x20           /* LSR THRE */
	jnz  5f
	loop 4b
	jmp  2b                 /* タイムアウト: この文字は COM1 へ出さない */
5:	mov  al, ah
	mov  dx, COM1
	out  dx, al
	jmp  2b
3:	pop  dx
	pop  cx
	pop  bx
	pop  ax
	ret

drive:  .byte 0
/* INT 13h 拡張の DAP(1 セクタずつ KSEG:dap_off へ。LBA は 16bit で足りる) */
dap:    .byte 0x10, 0
        .word 1
dap_off: .word 0
        .word KSEG
dap_lba: .long 0, 0
tries:  .byte 4
lba:    .word 0
bufoff: .word 0
spt:    .word 18
msg_banner: .asciz "tizix stage2\r\n"
msg_go:     .asciz "stage2: entering kernel\r\n"
msg_err:    .asciz "stage2: kernel load error\r\n"
