/* arch/x86-ia16/bios13.s  --  FatFs 下位層のための INT 13h ラッパ
 *   QEMU では SeaBIOS が INT 13h を提供する。1.44MB フロッピー幾何 18/2 決め打ち。
 *   バッファは tiny モデルなので ES=DS=kernel セグメント、near オフセットで渡る。
 *   LBA は 16bit(フロッピー 2880 セクタで十分)。
 *   ABI: ia16-gcc default(引数右→左スタック、戻り AX、呼び出し側掃除、
 *        SI/DI/BP callee-saved)。
 *
 *   ★2026-09-12: bios13_reset/bios13_rw は以前 sti で割り込みを有効化して
 *   BIOS を呼んでいた(意図はおそらく長いフロッピー I/O 中に操作性を保つ
 *   ため)。しかし bios13_rw はマルチセクタ転送をループで回しており、その
 *   間ずっと割り込み有効 = 100Hz タイマ(_isr08)が何度も割り込んで
 *   スケジューラのプリエンプションが起き得る。FatFs(ff.c、FF_FS_TINY=1 の
 *   共有 win[] バッファ)も src/vfs.c 等のカーネル状態も再入可能設計では
 *   ないため、disk_read の途中で別コンテキストへ切り替わると壊れる。
 *   実害: `ls -l` の直後に `cat` すると f_open は内部的に成功(FR_OK)して
 *   いるのに呼び出し元へ結果が正しく伝わらない、という再現性の低い(タイミ
 *   ング依存の)症状が出ていた。BIOS 呼び出し中も割り込み禁止のままにする
 *   (int 0x80 自体が IF を落として ISR に入る Z80 側の di 区間と同じ扱いに
 *   揃える)。 */
	.code16
	.intel_syntax noprefix
	.text
	.global bios13_reset
	.global bios13_rw

/* void bios13_reset(unsigned drive);   [bp+4]=drive */
bios13_reset:
	push bp
	mov  bp, sp
	push dx
	mov  dl, [bp+4]
	xor  ah, ah              /* AH=00 : reset */
	int  0x13
	pop  dx
	pop  bp
	ret

/* unsigned bios13_rw(unsigned drive, unsigned lba, unsigned count,
 *                    void *buf, unsigned op);
 *   op = 2 : read (AH=02)  /  op = 3 : write (AH=03)
 *   [bp+4]=drive [bp+6]=lba [bp+8]=count [bp+10]=buf [bp+12]=op
 *   戻り: 0=成功 / INT13 エラーコード(AH)
 */
bios13_rw:
	push bp
	mov  bp, sp
	push si
	push di
	push bx
	push cx
	push dx

	push ds
	pop  es                  /* ES = DS */
	mov  di, [bp+8]          /* di = 残りセクタ数 */
	mov  si, [bp+6]          /* si = 現在 LBA */
	mov  bx, [bp+10]         /* bx = バッファオフセット */

.Lrw:
	or   di, di
	jz   .Lok
	mov  ax, si
	xor  dx, dx
	mov  cx, 18
	div  cx                  /* ax=track, dx=sector-1 */
	mov  cl, dl
	inc  cl                  /* CL = sector (1..18) */
	shr  ax, 1               /* ax=cylinder, CF=head bit */
	mov  ch, al             /* CH = cylinder low 8 */
	mov  dh, 0
	adc  dh, 0              /* DH = head (0/1) */
	mov  dl, [bp+4]         /* DL = drive */
	mov  al, 1             /* AL = 1 sector */
	mov  ah, byte ptr [bp+12]  /* AH = op (2 read / 3 write) */
	int  0x13
	jc   .Lerr
	add  bx, 512
	inc  si
	dec  di
	jmp  .Lrw

.Lok:
	xor  ax, ax
	jmp  .Lret
.Lerr:
	mov  al, ah
	xor  ah, ah
.Lret:
	pop  dx
	pop  cx
	pop  bx
	pop  di
	pop  si
	pop  bp
	ret
