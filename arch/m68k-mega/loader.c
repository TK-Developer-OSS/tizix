/* arch/m68k-mega loader.c - 外部コマンドのロード(src/loader.h の plat_load / plat_ctx)
 *
 *   #47 で実装、#50 で複数スロット化、**#61 で PIC 化**。空きスロット探し・argv[] の
 *   組み立て・プロセス表への登録は全アーキ共通の src/kexec.c にあり、ここには
 *   68000 とこのボードに依る 2 点(像の置き方、最初の文脈の形)だけを置く。
 *
 *   以前は「固定アドレスに固定リンク、スロットごとにベースをずらす」再配置ゼロの
 *   構成だった。そのため (a) コマンドごとにスロット数ぶんの .bin を再リンクする
 *   必要があり(ls1.bin/ls2.bin/…)、(b) Makefile とカーネルの枠数を手で同期させる
 *   約束事が残り、(c) 使える番地がリンク時に決まってしまっていた。
 *
 *   いまはコマンドを `-mpcrel`(68000 の PC 相対)でコンパイルし、VMA=0 で
 *   1 本だけリンクする(user/cmd.ld)。**ロード時に像を書き換えない**ので
 *   [[loadtime-reloc-forbidden]] の地雷は踏まない ── 68000 は PC 相対アドレッシングを
 *   持つので、z80 の iy_reg のような後処理をせずにコンパイラの正規パスで位置独立
 *   コードが出る(#61 の調査で ls.c の絶対再配置 R_68K_32 x32 が R_68K_PC16 x29 になり、
 *   絶対参照が消えることを確認)。crt0cmd.s も手書きなので PC 相対だけで書いてある。
 *
 *   残る制約は PC 相対変位が 16bit = ±32KB であること。像(コード+データ+BSS)が
 *   32KB 以内なら中のどこへでも届く。IMG_BUDGET は 24KB なので制約にならない。
 *
 *   スロットの形(番地は include/plat.h の PLAT_SLOT_ADDR / PLAT_SLOT_SIZE):
 *     base                 像(.bin をそのまま)。先頭 32B はプロセスの見出し(src/phdr.h。
 *                          user/cmd.ld が 0 で空ける)、base + 0x20 が crt0cmd の _start
 *     base + IMG_BUDGET    argv[] と引数文字列(src/kexec.c が書く)
 *     …                    スタック(頂上から下へ。約 7KB。FatFs の呼び出し深度に対して
 *                          十分な余裕: 実測は ls / 相当で数百 B 程度)
 *     base + PLAT_SLOT_SIZE  頂上。偽コンテキストはこの直下
 */
#include "loader.h"
#include "kmem.h"
#include "phdr.h"

#define IMG_BUDGET   0x6000UL              /* 24KB。**像 + BSS** の上限(コードだけではない)。
                                           * user/cmd.ld の ASSERT と一致させること ──
                                           * 超えると crt0cmd の BSS クリアがここに置いた argv[] を
                                           * 消し、コマンドが「引数なし」で起動する(vi で実際に踏んだ)。 */
#define CTX_SIZE     0x42UL                /* D0-D7/A0-A6(60B)+SR(2B)+PC(4B) */

/* #61: カーネル(slot 0)のスタックは link-kernel.ld の __stack_top =
 * 0x100000 から下へ伸びる。最上位スロットの上端がそこへ食い込むと、
 * 症状が「たまに落ちる」形で出て追いにくいので、16KB を予約したうえで
 * 越えたらビルドを止める。 */
#define KSTACK_RESERVE  0x4000UL
typedef char proc_area_fits[(PLAT_SLOT_ADDR(KW_NSLOT - 1) + PLAT_SLOT_SIZE
                             <= 0x100000UL - KSTACK_RESERVE) ? 1 : -1];
/* argv[] と文字列が、像の上限と頂上の間に収まること(残りがスタック)。 */
typedef char argv_area_fits[(IMG_BUDGET + KEXEC_ARGV_BYTES + CTX_SIZE + 0x1000UL
                             <= PLAT_SLOT_SIZE) ? 1 : -1];

/* 何スロット要るか(#113)。m68k は 1 スロット 32KB に像を丸ごと置く作りのまま(IMG_BUDGET と
 * user/cmd.ld の ASSERT がビルド時に止める)。PC 相対の変位が ±32KB なので、像を複数スロットに
 * 広げるには作りの見直しが要る。今は常に 1。 */
unsigned char plat_need(FIL *fp)
{
    (void)fp;
    return 1;
}

unsigned char plat_load(FIL *fp, unsigned char n, unsigned char k, struct kimage *im)
{
    unsigned long base = PLAT_SLOT_ADDR(n);
    UINT br;

    (void)k;

    if ((unsigned long)f_size(fp) > IMG_BUDGET)
        return 0;
    f_read(fp, (void *)base, (UINT)IMG_BUDGET, &br);

    im->entry = base + PH_SIZE;            /* 先頭 32B はプロセスの見出し(src/phdr.h)、その直後が crt0cmd の _start */
    im->argv  = base + IMG_BUDGET;
    im->top   = base + PLAT_SLOT_SIZE;
    return 1;
}

/* 偽コンテキストは crt0.s の irq6_handler / trap0_handler が使う保存形式と
 * 完全に一致させる必要がある(movem.l %d0-%d7/%a0-%a6 の並び + SR:PC)。
 * ここが崩れると起動直後に Address Error 等でハングするので、フィールド
 * 順序を変えたら crt0.s 側も必ず合わせて直すこと。 */
unsigned long plat_ctx(const struct kimage *im, unsigned long argc, unsigned long argv)
{
    unsigned long sp = im->top - CTX_SIZE;
    unsigned long *ctx = (unsigned long *)sp;
    unsigned k;

    for (k = 0; k < 15; k++)
        ctx[k] = 0;
    ctx[0] = argc;                                   /* D0 = argc */
    ctx[8] = argv;                                   /* A0 = &argv[0] */
    *(unsigned short *)(sp + 60) = 0x2000;           /* SR: S=1, 割込みマスク=0 */
    *(unsigned long  *)(sp + 62) = im->entry;        /* PC = crt0cmd _start */
    return sp;
}
