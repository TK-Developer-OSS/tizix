/* arch/x86-ia16/plat.h  --  共有 src/ を ia16-gcc で通すための移植シム
 *   ia16 ビルドだけ CFLAGS の -include でこれを前置する(Makefile 参照)。
 *   Z80(sdcc)ビルドはこのファイルを一切見ない。
 */
#ifndef _PLAT_X86_IA16_H
#define _PLAT_X86_IA16_H

/* SDCC の呼出規約属性は x86 では無意味 → 消す */
#define __sdcccall(n)
#define __z88dk_fastcall
#define __z88dk_callee
#define __naked
#define __critical

/* IRQ_OFF()/IRQ_ON() は src/kmem.h が arch 別に定義する */

#endif
