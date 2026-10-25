/* tizix x86-i386-unreal : stage1 = VBR(セクタ0, 512B)
 *   役割は「gap 領域に置いた stage2 をロードして飛ぶ」だけ。
 *
 *   このセクタは FAT12 ボリュームの VBR。先頭 0x00..0x3D は
 *   jmp(EB 3C 90)+ OEM 名 + BPB で、mkfs.fat が書いたものを残す。
 *   Makefile が stage1.bin の 0x3E..0x1FD(コード)だけを FAT イメージの
 *   セクタ0 に上書きする(BPB と 0x1FE の 0x55AA は温存)。
 *   → よって _start は file offset 0x3E に置く。
 *
 *   フロッピー配置:
 *     LBA 0        stage1 (このセクタ)
 *     LBA 1 .. 8   stage2 (4KB 予約)
 *     LBA 9 ..     kernel blob
 *     LBA 64 ..    FAT12 データ領域(mkfs.fat -R 64)
 */
	.code16
	.intel_syntax noprefix
	.section .text
	.global _start

STAGE2_LBA  = 1
STAGE2_SECS = 8
STAGE2_SEG  = 0x0800

	.skip 0x3e, 0           /* 0x00..0x3D は BPB。mkfs のものを使う */

_start:
	cli
	xor  ax, ax
	mov  ds, ax
	mov  es, ax
	mov  ss, ax
	mov  sp, 0x7c00
	cld
	mov  [drive], dl

	cmp  dl, 0x80
	jb   chs_rd
	/* HDD / USB メモリ / SD カード(BIOS からはドライブ 0x80 以降): INT 13h 拡張(LBA)で stage2 を読む */
	mov  byte ptr [tries], 4
lba_rd:
	mov  si, offset dap
	mov  ah, 0x42
	mov  dl, [drive]
	int  0x13
	jnc  rd_ok
	xor  ah, ah
	mov  dl, [drive]
	int  0x13               /* reset して再試行 */
	dec  byte ptr [tries]
	jnz  lba_rd
	jmp  err
chs_rd:
	mov  byte ptr [tries], 4   /* 実機の FDD は一過性エラーがあるのでリトライ */
rd:
	mov  ax, STAGE2_SEG
	mov  es, ax
	xor  bx, bx
	mov  ah, 0x02
	mov  al, STAGE2_SECS
	mov  ch, 0
	mov  cl, STAGE2_LBA + 1     /* sector = LBA+1 (1-based), track0/head0 */
	mov  dh, 0
	mov  dl, [drive]
	int  0x13
	jnc  rd_ok
	xor  ah, ah
	mov  dl, [drive]
	int  0x13               /* reset して再試行 */
	dec  byte ptr [tries]
	jnz  rd
	jmp  err
rd_ok:

	mov  dl, [drive]
	ljmp STAGE2_SEG, 0x0000

err:
	mov  si, offset msg
1:	lodsb
	test al, al
	jz   2f
	mov  ah, 0x0e
	mov  bx, 0x0007
	int  0x10
	jmp  1b
2:	hlt
	jmp  2b

drive: .byte 0
/* INT 13h 拡張の DAP: stage2(LBA STAGE2_LBA から STAGE2_SECS セクタ)を STAGE2_SEG:0 へ */
dap:   .byte 0x10, 0
       .word STAGE2_SECS
       .word 0, STAGE2_SEG
       .long STAGE2_LBA, 0
tries: .byte 4
msg:   .asciz "VBR: stage2 load error\r\n"

	.space 510-(.-_start+0x3e), 0
	.word  0xaa55
