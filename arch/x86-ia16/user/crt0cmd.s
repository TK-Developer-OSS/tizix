/* arch/x86-ia16/user/crt0cmd.s : x86 ユーザーコマンドの C ランタイム入口
 *   kexec_file が pseg:0x0100 にロードし、DS=ES=SS=CS=pseg / IP=0x0100 で進入。
 *   引数文字列は pseg:0xFF00(スペース区切り、1本の C 文字列)。
 *   argv_init.c の build_argv() でその場 NUL 化してトークン配列 g_argv[] を
 *   作り、main(argc, argv) を呼ぶ。戻ったら INT 80h AH=0 で exit。
 *
 *   ★2026-09-12: user/cmd.ld は .bss を NOLOAD にして __bss_start/__bss_end
 *   シンボルまで用意していたのに、この crt0 が一度もゼロクリアしていなかった。
 *   .bin ファイルには .bss の実体バイトが無い(NOLOAD なので)ため、静的変数
 *   (tzstdio.h の tz_files[] 等)はロード時点でそのセグメントに残っていた
 *   "前にそこで走っていた別コマンドのメモリの残骸" を初期値として持つ。
 *   実害: `ls -l` の直後に `cat` すると、tz_files[].used がゼロ初期化されて
 *   いる前提のスロット空き判定が誤り、fopen が(カーネル側は成功しているのに)
 *   NULL を返す事故が起きていた(再現性が高く、コマンドの組み合わせ・直前の
 *   メモリ使用量に依存する)。 */
	.code16
	.intel_syntax noprefix
	.section .text.start,"ax",@progbits    /* リンカスクリプトが最先頭に置く */
	.global _start
	.global _exit

_start:
	mov  ax, offset __bss_start
	mov  di, ax
	mov  cx, offset __bss_end
	sub  cx, ax
	jz   1f
	xor  al, al
	cld
	rep  stosb                /* [__bss_start, __bss_end) を 0 埋め */
1:
	mov  ax, 0xFF00
	push ax                  /* build_argv(raw) の引数 */
	call build_argv          /* ax = argc(cdecl: 呼び出し側がスタック掃除) */
	add  sp, 2
	mov  cx, ax               /* argc を退避(次の push で ax を使い回すため) */
	mov  bx, offset g_argv
	push bx                  /* main 第2引数: argv(先に積む → main から見て
	                          ; 遠い side = argv[](2番目の実引数)になる) */
	push cx                  /* main 第1引数: argc(最後に積む → 一番近い =
	                          ; 1番目の実引数。旧コードと同じ push 順) */
	call main
	/* 戻り値(AX)は捨てて exit */

_exit:
	mov  ax, 0               /* AH=0 : exit syscall */
	int  0x80
1:	hlt
	jmp  1b
