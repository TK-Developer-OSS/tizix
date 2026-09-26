/* arch/x86-ia16/proc.s : プロセス生成の補助
 *   farcpy : カーネルセグメントの near バッファ -> 任意セグメント へコピー。
 *            kexec_file が .BIN / 引数 / 偽コンテキストをプロセスセグメントへ
 *            書き込むのに使う。
 *   ABI: ia16-gcc default(引数右→左スタック、SI/DI/BP callee-saved、戻り AX)。
 */
	.code16
	.intel_syntax noprefix
	.text
	.global farcpy

/* void farcpy(unsigned dseg, unsigned doff, const void *src, unsigned n);
 *   [bp+4]=dseg [bp+6]=doff [bp+8]=src(near, DS 相対) [bp+10]=n
 */
	.global farcpy_in

farcpy:
	push bp
	mov  bp, sp
	push si
	push di
	push es
	mov  es, [bp+4]
	mov  di, [bp+6]
	mov  si, [bp+8]
	mov  cx, [bp+10]
	cld
	rep  movsb            /* DS:SI -> ES:DI */
	pop  es
	pop  di
	pop  si
	pop  bp
	ret

/* void farcpy_in(void *dst, unsigned sseg, unsigned soff, unsigned n);
 *   任意セグメント sseg:soff -> カーネルの near バッファ dst へ。
 *   [bp+4]=dst [bp+6]=sseg [bp+8]=soff [bp+10]=n
 *
 *   ★2026-09-12: 旧版は「呼び出し時に ES がカーネルセグメントである」ことを
 *   コメントで前提にするだけで、自分では ES を設定していなかった。
 *   _isr80(INT 80h)経由の呼び出しは ES=KSEG を保証しているので無事だったが、
 *   kexec_file(sh のメインループから直接呼ばれ、割込みハンドラを経由しない)
 *   からの呼び出しでは ES が別プロセスのセグメントに残っている可能性があり、
 *   dst(near ポインタ、DS=SS=カーネル前提)への書き込み先が化ける。
 *   farcpy(書き込み方向)は自分で ES=dseg を張るので元々安全だったが、
 *   farcpy_in(読み込み方向)は非対称に ES 管理を怠っていた。
 *   呼び出し元の ES に依存しないよう、自分で ES=DS(呼び出し時点のカーネル
 *   セグメント。tiny モデルでカーネルは常に CS=DS=SS)を張るよう修正。 */
farcpy_in:
	push bp
	mov  bp, sp
	push si
	push di
	push ds
	push es
	mov  ax, ds
	mov  es, ax          /* ES = 呼び出し時の DS(カーネルセグメント) */
	mov  di, [bp+4]      /* ES:DI = kernel:dst */
	mov  ax, [bp+6]
	mov  ds, ax          /* DS = sseg */
	mov  si, [bp+8]
	mov  cx, [bp+10]
	cld
	rep  movsb           /* sseg:SI -> ES(kernel):DI */
	pop  es
	pop  ds
	pop  di
	pop  si
	pop  bp
	ret
