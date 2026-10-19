/* tizix x86-ia16 : kernel C runtime entry + スケジューラ機構
 *   stage2 が 0x1000:0000 へ far jump してくる。CS=0x1000。
 *   DS=ES=SS=CS に揃え、SP をセグメント上端に、BSS をゼロクリアして kmain へ。
 *
 *   プロセスモデル(Z80 の block モデルの x86 版):
 *     slot n (0..7) ↔ セグメント (n+1)*0x1000  (slot0 = kernel/idle, 0x1000)
 *     kwork[] (kmem.h): pid_tbl[8]@+0, sp_tbl[8]@+8 (SP のみ, SS は slot から
 *                       導出), current@+0x18。pid==0 は空き。
 *   タイマ: 8254 PIT ch0 100Hz -> IRQ0 -> INT 08h = _isr08(退避→pick→復帰)。
 *   syscall: INT 80h = _isr80 (AH=0 exit /1 putc /2 getc /3 getticks)。
 */
	.code16
	.intel_syntax noprefix
	.section .text._start,"ax",@progbits
	.global _start
	.global _isr08
	.global _isr80

PIT_DIV   = 11932           /* 1193182 / 100 */
KSEG      = 0x1000
/* kwork 内のオフセット(src/kmem.h の x86 節と同じ式。X86_N は Makefile が plat.h の X86_MAXSLOT から渡す) */
KW_SP     = X86_N
KW_CUR    = 3 * X86_N
KW_SS     = 6 * X86_N + 0x10A

_start:
	cli
	mov  ax, cs
	mov  ds, ax
	mov  es, ax
	mov  ss, ax
	mov  sp, 0xfffe
	cld

	/* ---- IVT: INT 08h -> _isr08, INT 80h -> _isr80 (IVT はセグメント0) ---- */
	xor  ax, ax
	mov  es, ax
	mov  word ptr es:0x20, offset _isr08
	mov  word ptr es:0x22, cs
	mov  word ptr es:0x200, offset _isr80   /* 0x80*4 */
	mov  word ptr es:0x202, cs
	mov  ax, cs
	mov  es, ax

	/* ---- 8254 PIT ch0: mode 3, 100Hz ---- */
	mov  al, 0x36
	out  0x43, al
	mov  al, PIT_DIV & 0xFF
	out  0x40, al
	mov  al, PIT_DIV >> 8
	out  0x40, al

	/* ---- PIC1: IRQ0 アンマスク ---- */
	in   al, 0x21
	and  al, 0xFE
	out  0x21, al

	/* ---- BSS ゼロクリア ---- */
	xor  ax, ax
	mov  di, offset __bss_start
	mov  cx, offset __bss_end
	sub  cx, di
	jbe  1f
	rep  stosb
1:
	/* ---- PCB: pid_tbl[0] = 1 (idle/シェル文脈、常時 runnable) ---- */
	mov  byte ptr [kwork], 1

	call kmain
2:	hlt
	jmp  2b

	.section .text

/* ================================================================
 * _isr08 : IRQ0 タイマ。退避 -> tick -> round-robin pick -> 復帰。
 *   入場時 SS:SP = 現プロセスのスタック。DS/ES は現プロセスのもの。
 * ================================================================ */
_isr08:
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push bp
	push ds
	push es
	mov  bp, sp
	mov  cx, ss                 /* cx = 現 SS(退避対象。DS を潰す前に) */
	mov  ax, KSEG
	mov  ds, ax
	mov  es, ax
	/* ss_tbl[current] = 現 SS / sp_tbl[current] = bp */
	mov  bl, [kwork + KW_CUR]     /* current */
	xor  bh, bh
	shl  bx, 1
	mov  [kwork + KW_SP + bx], bp
	mov  [kwork + KW_SS + bx], cx

	call plt_interrupt          /* 既存 C: tick++/epoch */
	mov  al, 0x20
	out  0x20, al              /* PIC1 EOI */

	/* round-robin: current を進め、pid_tbl!=0 の slot を探す */
	mov  al, [kwork + KW_CUR]
rr_lp:
	inc  al
	cmp  al, byte ptr [x86_nslot_rt]   /* 実際に使うスロット数(起動時に kmain が設定) */
	jb   rr_chk
	xor  al, al               /* wrap -> slot0 (idle, 常時 runnable) */
rr_chk:
	mov  bl, al
	xor  bh, bh
	cmp  byte ptr [kwork + bx], 0
	je   rr_lp                 /* free -> skip */
	mov  [kwork + KW_CUR], al    /* current = al */

	/* 復帰: SS/SP = ss_tbl[current] / sp_tbl[current] */
	mov  bl, al
	xor  bh, bh
	shl  bx, 1
	mov  dx, [kwork + KW_SP + bx]       /* dx = 保存 SP */
	mov  cx, [kwork + KW_SS + bx]   /* cx = 保存 SS */
	cli
	mov  ss, cx
	mov  sp, dx
	pop  es
	pop  ds
	pop  bp
	pop  di
	pop  si
	pop  dx
	pop  cx
	pop  bx
	pop  ax
	iret

/* ================================================================
 * _isr80 : INT 80h システムコール
 *   AH = 番号。0 = exit(コンテキストスイッチが要るので asm 直)。
 *   1.. = sys_call(ax, bx, cx, si, di, cseg) へ(C。sysfile.c)。
 *
 *   ★ -mcmodel=tiny の C は SS==DS==CS を前提にする。int 80h 入場時は
 *     SS=DS=呼び出し元セグメント。ここで DS/ES/SS を KSEG に揃え、専用の
 *     カーネル syscall スタックへ切り替えてから C を呼ぶ。復帰時に戻す。
 *   int で IF=0 済み。syscall はアトミック(sti しない)。
 * ================================================================ */
/* PERSLOT=1: syscall スタック/退避欄をスロットごとに持つ(syscall 中に待てる。パイプ用)。
 * PERSLOT=0: 従来の単一スタック(CONSOLE=vga は #182 のため 0)。Makefile が -Wa,--defsym で渡す。 */
_isr80:
	push ax                   /* [bp+16] func<<8 | arg  (呼び出し元スタック) */
	push bx                   /* [bp+14] */
	push cx                   /* [bp+12] */
	push dx                   /* [bp+10] */
	push si                   /* [bp+8]  */
	push di                   /* [bp+6]  */
	push bp                   /* [bp+4]  */
	push ds                   /* [bp+2]  */
	push es                   /* [bp+0]  */
	mov  bp, sp               /* bp -> 呼び出し元スタックの退避フレーム */

	mov  ax, KSEG
	mov  ds, ax               /* DS = KSEG(globals/kwork 参照用) */

	mov  ax, [bp+16]
	test ah, ah
	jz   sc_exit              /* AH=0 : exit(SS 切替不要) */

	/* --- 呼び出し元 SS:frame と syscall 引数を kernel globals へ --- */
.if PERSLOT
	mov  bl, [kwork + KW_CUR]
	xor  bh, bh
	shl  bx, 1                /* bx = current*2(スロットごとの退避欄の添字) */
	mov  ax, ss
	mov  [sc_css + bx], ax
	mov  [sc_csp + bx], bp
.else
	mov  ax, ss
	mov  [sc_css], ax
	mov  [sc_csp], bp
.endif
	mov  ax, [bp+16]
	mov  [sc_ax], ax
	mov  ax, [bp+14]
	mov  [sc_bx], ax
	mov  ax, [bp+12]
	mov  [sc_cx], ax
	mov  ax, [bp+8]
	mov  [sc_si], ax
	mov  ax, [bp+6]
	mov  [sc_di], ax
	mov  bl, [kwork + KW_CUR]
	xor  bh, bh
	shl  bx, 1
	mov  bx, [x86_slot_seg + bx]   /* スロットのセグメント(4KB ブロックのプールから割り当てた先頭。slot0 = 0x1000) */
	mov  [sc_cseg], bx

	/* --- カーネル syscall スタックへ切替 --- */
	mov  ax, KSEG
	mov  ss, ax
	mov  es, ax
.if PERSLOT
	/* スロットごとに専用の syscall スタック(1KB)。syscall 中に待つ(getchar / pipe)と
	 * 別プロセスへ切り替わるので、単一スタックだと互いに壊し合う(#143)。 */
	mov  bl, [kwork + KW_CUR]
	xor  bh, bh
	inc  bx
	mov  cl, 10
	shl  bx, cl               /* bx = (current+1)*1024 */
	add  bx, offset sc_stack
	mov  sp, bx
.else
	mov  sp, offset sc_stack_top
.endif

	push word ptr [sc_cseg]
	push word ptr [sc_di]
	push word ptr [sc_si]
	push word ptr [sc_cx]
	push word ptr [sc_bx]
	push word ptr [sc_ax]
.if PERSLOT
	call sys_call
	add  sp, 12

	/* --- 呼び出し元スタックへ戻す(待ちの間に他プロセスが走っても、退避欄はスロットごと)--- */
	cli
	mov  bl, [kwork + KW_CUR]   /* 復帰時の current = 自分のスロット */
	xor  bh, bh
	shl  bx, 1
	mov  cx, [sc_css + bx]
	mov  dx, [sc_csp + bx]
	mov  ss, cx
	mov  sp, dx
	mov  bp, sp
	mov  [bp+16], ax          /* 戻り値 -> 呼び出し側 AX */
.else
	call sys_call
	add  sp, 12
	mov  [sc_ret], ax

	/* --- 呼び出し元スタックへ戻す --- */
	cli
	mov  ax, [sc_css]
	mov  ss, ax
	mov  sp, [sc_csp]
	mov  bp, sp
	mov  ax, [sc_ret]
	mov  [bp+16], ax          /* 戻り値 -> 呼び出し側 AX */
.endif

	pop  es
	pop  ds
	pop  bp
	pop  di
	pop  si
	pop  dx
	pop  cx
	pop  bx
	pop  ax
	iret

/* ---- exit: slot 解放 -> round-robin で次へ(現コンテキストは破棄)---- */
sc_exit:
	cli
	mov  bl, [kwork + KW_CUR]
	xor  bh, bh
	mov  byte ptr [kwork + bx], 0   /* pid_tbl[current] = 0 (free) */
	/* 次を pick して復帰(退避はしない = 死んだ文脈)。_isr08 の pick を再利用。 */
	mov  al, [kwork + KW_CUR]
	jmp  rr_lp

/* ---- _isr80 用 kernel syscall スタック + 退避 globals(.bss = 起動時ゼロ)---- */
	.data
	.global x86_nslot_rt
x86_nslot_rt: .byte 1            /* 使うスロット数(スケジューラの巡回範囲)。kmain が x86_nslots() に更新 */
	.section .bss
	.align 2
.if PERSLOT
sc_css:   .space 2 * X86_N   /* スロットごとの退避 SS / SP */
sc_csp:   .space 2 * X86_N
.else
sc_css:   .space 2
sc_csp:   .space 2
.endif
sc_ax:    .space 2
sc_bx:    .space 2
sc_cx:    .space 2
sc_si:    .space 2
sc_di:    .space 2
sc_cseg:  .space 2
sc_ret:   .space 2
.if PERSLOT
sc_stack: .space 1024 * X86_N   /* スロットごと 1KB */
.else
sc_stack: .space 1024
sc_stack_top:
.endif
