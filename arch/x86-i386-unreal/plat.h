/* arch/x86-i386-unreal/plat.h  --  共有 src/ を ia16-gcc で通すための移植シム
 *   ia16 ビルドだけ CFLAGS の -include でこれを前置する(Makefile 参照)。
 *   Z80(sdcc)ビルドはこのファイルを一切見ない。
 */
#ifndef _PLAT_X86_IA16_H
#define _PLAT_X86_IA16_H

/* SDCC の呼出規約属性は x86 では無意味 → 消す */
/* プロセススロット(テーブル)の最大数。実際に使う数は起動時のメモリ量から決まる(x86_nslots)。
 * crt0.s へは Makefile が plat.h を読んで -Wa,--defsym,X86_N= で渡し、同じ式で KW_* を計算する。 */
/* 24 → 20(2026-10-24): x86-ia16 のコマンド・ドライバ(mcpd 用 syscall、時刻、終了コード)を取り込んだら __bss_end が 0xFDC6 になり、
 * カーネル本体のスタック(0xFFFE から下へ)の余りが約 570B と、実測の使用量(約 630B)を下回った。シュリンク(fat12min / KOPT /
 * 外部シェル)は持ち込まない方針なので、syscall スタック 4 本(2KB)を減らしてスタックに回す。 */
#define X86_MAXSLOT 20
/* スロットごとの syscall スタックの大きさ = 1<<X86_SCSTK_SHIFT バイト(9 → 512B)。使用量の実測は 300B 前後
 * (free の「syscall stack headroom」で最小の余りを見られる)。カーネルの 64KB セグメントに全スロット分を収める。 */
#define X86_SCSTK_SHIFT 9

#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

/* IRQ_OFF()/IRQ_ON(): 元の IF を覚えて戻す(入れ子可)。以前は IRQ_ON が無条件の sti だったため、
 * IF=0 で走る syscall の途中(io.c の kputchar などの IRQ_OFF/ON 対)で割り込みが有効になり、
 * タイマ ISR が別プロセスへ切り替え → そのプロセスの syscall が単一の sc_stack を上書き →
 * 元の syscall のフレームが壊れる(`a &` `b &` で B しか出ない症状)。
 * 対の無い IRQ_ON()(kernel_init の割り込み解禁)は従来どおり sti。 */
extern unsigned char x86_irq_depth, x86_irq_was;
#define IRQ_OFF() do { unsigned _f; \
	__asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(_f) : : "memory", "cc"); \
	if (x86_irq_depth++ == 0) x86_irq_was = (_f & 0x200) ? 1 : 0; } while (0)
#define IRQ_ON()  do { \
	if (x86_irq_depth == 0) __asm__ volatile ("sti" ::: "memory"); \
	else if (--x86_irq_depth == 0 && x86_irq_was) __asm__ volatile ("sti" ::: "memory"); } while (0)

/* 待ちループ(kgetchar / con_break / sh の前景待ち)用の譲り。sti の直後に cli が
 * 来ると x86 は sti の 1 命令シャドウで割り込みを受けないため、タイマ(IRQ0)が
 * 一度も取れず、子プロセスが走らずハングする(起動直後の echo/ls/mkdir、2026-10-18 に
 * 採取: TICKS が止まり pid_tbl[子]=生存のまま)。hlt で次の割り込み(最大 10ms)を
 * 必ず待つ。 */
/* 2026-10-24: sti; hlt; cli は次の tick まで CPU を握ったまま止まるので、int 81h で次のスロットへ譲る x86_kyield(crt0.s)にした。
 * 全員が待ちのときだけ x86_kyield の中で hlt する。 */
extern void x86_kyield(void);
#define KYIELD()   do { x86_kyield(); __asm__ volatile ("" ::: "memory"); } while (0)

#endif

