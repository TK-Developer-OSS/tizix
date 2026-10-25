/* arch/x86-i386-unreal/xmem.s  --  アンリアルモードで 1MB 超の拡張メモリを読み書きする(386 以降)
 *
 *   一瞬だけプロテクトモードに入り、FS/GS に「ベース 0・リミット 4GB」の記述子を読み込んでから
 *   リアルモードへ戻る。リアルモードの mov fs/gs はリミットを書き換えないので、その後も
 *   FS:EDI / GS:EDI で 32bit アドレスにアクセスできる(アンリアルモード)。BIOS はそのまま使える。
 *   FS/GS は ia16-gcc も BIOS も使わないので、ES/DS の取り回し(#182)と干渉しない。
 *   実行できるコードは従来どおり 1MB 以内。ここで得るのはデータの置き場。
 *
 *   unsigned xmem_init(void)                 : 0 以外 = 成功(A20 有効化 + アンリアル化)
 *   void xmem_write(unsigned lo, unsigned hi, const void *src, unsigned n)
 *   void xmem_read (unsigned lo, unsigned hi, void *dst,       unsigned n)
 *     lo/hi = 物理アドレス(32bit)の下位/上位ワード。src/dst はカーネルセグメントの near ポインタ。n はバイト数。
 *   ABI: ia16-gcc default(引数右→左スタック、戻り AX、SI/DI/BP callee-saved、ES/DS は保存して戻す)。
 */
	.code16
	.arch i386
	.intel_syntax noprefix
	.text
	.global xmem_init
	.global x86_cpu_type
	.global xmem_write
	.global xmem_read

	.align 4
xgdt:
	.quad 0
	.quad 0x00CF92000000FFFF          /* selector 8: データ、ベース 0、リミット 4GB(G=1) */
xgdtr:
	.word 15
	.long 0

/* unsigned x86_cpu_type(void) : 0 = 8086/8088/186、2 = 286、3 = 386 以降。386 命令は 1 つも使わずに判定する。
 *   実モードでは FLAGS の bit12〜15 が 8086/186 では常に 1、286 では常に 0、386 以降では書き換えられる(IOPL/NT)。 */
x86_cpu_type:
	pushf
	xor  ax, ax
	push ax
	popf
	pushf
	pop  ax
	and  ax, 0xF000
	cmp  ax, 0xF000
	jne  1f
	xor  ax, ax                       /* bit12〜15 を 0 にできない = 8086/186 */
	jmp  3f
1:	mov  ax, 0x7000
	push ax
	popf
	pushf
	pop  ax
	and  ax, 0x7000
	jnz  2f
	mov  ax, 2                        /* 0x7000 を立てられない = 286 */
	jmp  3f
2:	mov  ax, 3
3:	popf
	ret

xmem_init:
	call x86_cpu_type                 /* 386 未満には lgdt/cr0 の 32bit 命令を実行させない(286 は #UD で止まる) */
	cmp  ax, 3
	jae  9f
	xor  ax, ax
	ret
9:	push bp
	mov  bp, sp
	push si
	push di
	pushf
	cli
	/* A20 を有効に(システムコントロールポート A 経由。QEMU・多くの実機で可) */
	in   al, 0x92
	or   al, 2
	and  al, 0xFE                     /* bit0 はリセットなので触らない */
	out  0x92, al
	/* GDT の物理アドレス = DS*16 + offset xgdt */
	xor  eax, eax
	mov  ax, ds
	shl  eax, 4
	movzx ebx, word ptr [xgdt_off]
	add  eax, ebx
	mov  dword ptr [xgdtr + 2], eax
	lgdt [xgdtr]
	mov  eax, cr0
	or   al, 1
	mov  cr0, eax
	jmp  1f
1:	mov  bx, 8
	mov  fs, bx
	mov  gs, bx
	mov  eax, cr0
	and  al, 0xFE
	mov  cr0, eax
	jmp  2f
2:	/* リアルモードへ戻った。FS/GS のリミットは 4GB のまま。簡易自己診断は C 側(xmem_selftest)で。 */
	mov  ax, 1
	popf
	pop  di
	pop  si
	pop  bp
	ret
xgdt_off: .word xgdt

/* void xmem_write(unsigned lo, unsigned hi, const void *src, unsigned n) */
xmem_write:
	push bp
	mov  bp, sp
	push si
	push di
	push cx
	mov  di, [bp+4]
	mov  ax, [bp+6]
	shl  eax, 16
	mov  ax, di
	mov  edi, eax                     /* edi = 物理アドレス */
	mov  si, [bp+8]
	mov  cx, [bp+10]
	jcxz 3f
1:	mov  al, [si]
	mov  gs:[edi], al
	inc  si
	inc  edi
	loop 1b
3:	pop  cx
	pop  di
	pop  si
	pop  bp
	ret

/* void xmem_read(unsigned lo, unsigned hi, void *dst, unsigned n) */
xmem_read:
	push bp
	mov  bp, sp
	push si
	push di
	push cx
	mov  si, [bp+4]
	mov  ax, [bp+6]
	shl  eax, 16
	mov  ax, si
	mov  esi, eax                     /* esi = 物理アドレス */
	mov  di, [bp+8]
	mov  cx, [bp+10]
	jcxz 3f
1:	mov  al, gs:[esi]
	mov  [di], al
	inc  esi
	inc  di
	loop 1b
3:	pop  cx
	pop  di
	pop  si
	pop  bp
	ret
