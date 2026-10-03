#ifndef _PLAT_H
#define _PLAT_H
/* ============================================================================
 * arch/m68k-mega ボードマップ
 *
 * 実機: 生の MC68000(4MHz, Mega2560 が /RESET・/HALT・/DTACK・クロック(D11=
 * OC1A)を駆動)。SRAM は 68000 のバスに直結(A20=0 側)、Mega は /DTACK の
 * みで介入しデータは駆動しない。
 *
 * デバイス空間は上位アドレス線 1 本ずつで選ぶ(2026-09-20 に A20 だけの
 * 方式から変更):
 *     A20=1            … デバイス空間(SRAM ではない、の意)
 *     A20=1 かつ A23=1 … UART      (0x900000)
 *     A20=1 かつ A22=1 … SD カード (0x500000)
 * ★A22 は Mega がソフトでデコードするだけでなく、**実機では A22 の反転が
 *   そのまま SD の CS になっている**。つまり CPU がこの空間を触っている間
 *   だけカードが選択される。CPU は /DTACK を待って止まったままアドレスを
 *   保持するので、Mega は 1 バスサイクルの中で 1 トランザクション
 *   (コマンド→応答→512B→ビジー)を完結させること。512B を 1 バイトずつ
 *   別サイクルで SPI 転送する実装にすると、サイクル間で CS が H に戻って
 *   壊れる。A22 にはプルダウンが要る(リセット中/転送中はバスが Hi-Z で
 *   浮き、反転して CS=L になってしまうため)。
 * 下位側は Mega が A1-A4 だけを見るので、各デバイス内はその範囲で
 * エイリアスする。
 *
 * バイトレーン判定は 68000 の規約どおり(偶数アドレス=UDS, 奇数=LDS)。
 * UART は UDS 側(偶数)=DATA、LDS 側(奇数)=STATUS。
 *
 * RAM: 実機は AS6C4008(512K x8)を2個、D0-7/D8-15 に振り分けて 16bit 幅化
 * (バイトレーンが UDS/LDS に対応)。よって総容量 1MB、ちょうど A20 の
 * デコード境界(0x000000-0x0FFFFF)と一致する。link-kernel.ld の MEMORY 長
 * と一致させること。
 * ========================================================================= */

#define RAM_ORIGIN      0x000000UL
#define RAM_SIZE        0x100000UL   /* 1MB(AS6C4008 x2、実機確定値) */

/* UART(Mega がビットバンで実装する MMIO) */
#define UART_DATA_ADDR   0x900000UL  /* A20|A23。偶数(UDS) : W=送信 / R=受信1バイト */
#define UART_STATUS_ADDR 0x900001UL  /* A20|A23。奇数(LDS) : R=状態のみ */
#define UART_TXRDY       0x01
#define UART_RXRDY       0x02

/* SD カード(Mega が SPI で実 SD を relay する MMIO)。
 * 設計原則: z80pack/z80board と同じく FatFs(ff.c)は 68000 側カーネルで
 * 動かし、Mega は FAT を一切知らない「生セクタの読み書き係」に徹する
 * (kernel が FatFs を代行する側、Mega は disk_read/disk_write の物理層)。
 *
 * レジスタはすべて 1 バイト幅(UART と同じ流儀)。Mega は CPU の SRAM を
 * 直接書けない(/DTACK のみ駆動、データは駆動しない)ため、DMA 的な一括
 * 転送はできない ── UART_DATA の RXRDY/TXRDY ポーリングと同じ「1バイトずつ
 * CPU 側が読み書きする」方式にする。これなら実機でもソフト検証(m68ksim)
 * でも同じ diskio.c で動く。
 *
 * 手順(read/write とも 512B/セクタ固定、複数セクタは diskio.c 側でループ):
 *   1. SD_LBA_HI/MID/LO へ 24bit LBA を書く(下位から書いても上位から書いて
 *      も良い。3 バイト揃った状態は CMD 書込み時にしか参照されない)。
 *   2. SD_CMD へ 1(read)か 2(write)を書く → トリガ。
 *   3. read: SD_STATUS の DATA_RDY を待って SD_DATA を読む、を 512 回。
 *      write: SD_STATUS の DATA_RDY(=受入可)を待って SD_DATA へ書く、を
 *      512 回。
 *   4. 転送完了後 SD_STATUS の ERROR を見て結果を確認する。
 * 24bit LBA は 512B セクタで 8G セクタ(4TB)まで足りるので実用上十分。 */
#define SDCARD_MMIO_BASE 0x500000UL  /* A20|A22。A22 の反転が実機の SD CS */
#define SD_LBA_HI    (SDCARD_MMIO_BASE + 0)  /*  8bit, W: LBA bit23-16 */
#define SD_LBA_MID   (SDCARD_MMIO_BASE + 1)  /*  8bit, W: LBA bit15-8 */
#define SD_LBA_LO    (SDCARD_MMIO_BASE + 2)  /*  8bit, W: LBA bit7-0 */
#define SD_CMD       (SDCARD_MMIO_BASE + 3)  /*  8bit, W: 1=read/2=write(トリガ) */
#define SD_STATUS    (SDCARD_MMIO_BASE + 4)  /*  8bit, R: bit0=ERROR bit1=DATA_RDY */
#define SD_DATA      (SDCARD_MMIO_BASE + 5)  /*  8bit, R/W: セクタ内の1バイト */
#define SD_ERROR     0x01
#define SD_DATA_RDY  0x02

/* レベル6周期割込み(Mega Timer5、周期は Makefile の TICK_HZ)。オートベクタ #30(offset 0x78)。 */
#define IRQ_TIMER_VECNO  30

/* ---- 共有 src/ に対するこのポートの素性 -------------------------------------
 *   src/ の分岐はアーキの名前ではなく、ここで名乗る PLAT_FLAT32 を見る
 *   (新しいポートを足すときに src/ の #if を書き足さずに済ませるため)。
 *
 *   PLAT_FLAT32        gcc の 32bit フラットなポートの一族。カーネルのワークは C 配列
 *                      kwork[](番地は src/kmem.h が積み上げる)、外部コマンドはスロット
 *                      1..N-1、コンソールは console.c の con_*、syscall は src/sysfile.c、
 *                      コマンドのロードは loader.c の plat_load / plat_ctx。
 *   PLAT_NSLOT         スロット数(slot0 = カーネル + init 込み)。**唯一の定義場所**
 *                      ── スケジューラの走査範囲も kwork の表の大きさもここから導く。
 *   PLAT_SLOT_ADDR(n)  スロット n(1..)のメモリの先頭。loader.c と、カーネルパイプの
 *                      バッファ枠を取る src/pipe.c が引く。
 *   PROC_SLOT_KB       1 スロットの大きさ(KB)。/bin/free の表示用。
 *
 *   slot 1..30 は 32KB ずつ、0x8000〜0xF8000。最上位の 16KB は **カーネル(slot 0)の
 *   スタック用に空けてある** ── link-kernel.ld の __stack_top = 0x100000 から下へ伸びるので、
 *   ここまでプロセスを置くと衝突する(loader.c の proc_area_fits がビルド時に見張る)。
 *   コマンドは PIC で .bin が 1 本なので、スロットを増やす代償は kwork の表だけ。 */
#define PLAT_FLAT32       1
#define PLAT_NSLOT        31
#define PLAT_SLOT_SIZE    0x8000UL
#define PLAT_SLOT_ADDR(n) ((unsigned long)(n) * 0x8000UL)
#define PROC_SLOT_KB      32

/* 割込みの禁止/許可。SR の割込みマスク(bit 8-10)だけ操作。他ビット(S/T)は触らない。 */
#define IRQ_OFF()  __asm__ volatile ("ori.w  #0x0700,%%sr" ::: "memory")
#define IRQ_ON()   __asm__ volatile ("andi.w #0xf8ff,%%sr" ::: "memory")
/* #94: 待ちループ(sh の前景待ち・入力待ち・init・getticks)から自分で譲る。
 * TRAP #1 = crt0.s の trap1_handler(タイマ割込みと同じ save/pick/restore、
 * tick は進めない)。以前は空で、実機のタイマ 1Hz だと sh が子を起動しても
 * 次の tick まで自分のスロットで回り続け、外部コマンド 1 回に約 1.5 秒かかった。 */
#define KYIELD()   __asm__ volatile ("trap #1" ::: "memory")

/* ---- 共有 src/ を m68k-elf-gcc で通すための移植シム(arch/x86-ia16/plat.h
 * と同じ役目)。SDCC 専用の呼出規約属性は m68k-elf-gcc では無意味 → 消す。 */
#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

#endif
