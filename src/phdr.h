/* src/phdr.h -- プロセスの見出し(a.out 領域)。PLAT_FLAT32 のアーキ(task.md #112)
 *
 *   各プロセスのデータ枠の先頭 32B(PLAT_SLOT_ADDR(n))を、そのプロセスの見出しにする。
 *   ここに眠り / 起きる時刻を置き、スケジューラ(src/kernel.c の slot_runnable)が
 *   読んで飛ばす。保護が無いので、起こす側は相手の欄を直接書けば足りる。
 *
 *   欄の位置は z80 の .BIN 先頭 32B 見出しと揃えてある(全アーキ共通に固定):
 *     0x10-0x12  z80: 'T' 'Z' 追加ブロック数(ビルド時)      … こちらでは使わない
 *     0x13       PH_STATE    u8   0 = 走れる / 1 = 眠っている(proc_block)
 *     0x14-0x17  PH_WAKEAT   u32  起きる時刻(カーネルの tick)。0 = 期限なし
 *     0x18       PH_WAKEPEND u8   眠る前に起こされた(起こしそこね防止。proc_block が消費)
 *     0x1C-0x1E  z80: kexec が書く imgtop / nblk               … こちらでは使わない
 *   残りは 0(将来用)。
 *
 *   像の側の約束: コマンドのリンカスクリプトがデータ枠の先頭 32B を 0 で空けておく
 *   (esp32: user/cmd.ld の .data、m68k: user/cmd.ld の .text の先頭)。ロードした時点で
 *   0 = 「走れる・期限なし」。slot0(カーネル / init)はデータ枠を持たないので、
 *   カーネルの中の 32B(src/kernel.c の kphdr0)を見出しにする。
 *   カーネルパイプのバッファ枠(PID_PIPEBUF)はプロセスではないので見出しを読まない
 *   (中身はパイプのデータで上書きされている)。
 */
#ifndef PHDR_H
#define PHDR_H

#define PH_SIZE      0x20
#define PH_STATE     0x13
#define PH_WAKEAT    0x14
#define PH_WAKEPEND  0x18

#define PH_RUN       0
#define PH_SLEEP     1

extern unsigned char kphdr0[PH_SIZE];

#define PHDR(n)          ((n) ? (volatile unsigned char *)PLAT_SLOT_ADDR(n) \
                              : (volatile unsigned char *)kphdr0)
#define PH_STATE_OF(n)   (PHDR(n)[PH_STATE])
#define PH_WAKEAT_OF(n)  (*(volatile unsigned long *)(PHDR(n) + PH_WAKEAT))
#define PH_PEND_OF(n)    (PHDR(n)[PH_WAKEPEND])

#endif
