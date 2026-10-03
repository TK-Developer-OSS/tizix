/* arch/m68k-mega/user/cmd.ld : 外部コマンドのリンカスクリプト
 *
 *   #61 で **PIC 化**した。以前はスロットごとに `--defsym PROC_BASE=` で
 *   違う VMA を渡し、同じ .o を再リンクしてスロット数ぶんの .bin を作って
 *   いた(ls1.bin / ls2.bin / …)。いまは VMA=0 の 1 本だけを作り、
 *   kexec が空いている番地へ置く。中身は -mpcrel でコンパイルした
 *   PC 相対コードなので、どこへ置いても動く(ロード時の書き換えは無い)。
 *
 *   VMA=0 にしてあるのは、`--oformat binary` が吐くファイルの先頭に
 *   パディングを作らないため。kexec.c はこのファイルを丸ごとロード先へ
 *   f_read するので、ファイル自体は先頭パディング無しの生イメージで
 *   なければならない。
 *
 *   ★ここに絶対アドレスを要求するセクション(.got / .got.plt 等)が
 *   現れたら PIC が壊れている合図。-mpcrel は GOT を作らないので、
 *   本来どちらも空のままになる。
 */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    .text 0 :
    {
        /* 先頭 32B はプロセスの見出し(src/phdr.h。眠り / 起きる時刻の欄)。0 で空けておき、
         * loader.c は base + 0x20 へ飛ぶ(#112) */
        . = 0x20;
        *(.text.start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
    }
    . = ALIGN(2);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) }
    . = ALIGN(2);
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame) }
}

/* ★像 + BSS が loader.c の IMG_BUDGET を超えていないことを **リンク時に**
 *   確かめる。超えるとどうなるか:
 *     kexec は argv[] とトークンプールを base + IMG_BUDGET に置く。
 *     一方 crt0cmd.s の _start は __bss_start..__bss_end を 0 クリアする。
 *     BSS がそこまで伸びていると、**起動直後に自分の argv を消す**。
 *   症状は「コマンドが引数なしで起動して usage を出す」で、原因から遠い。
 *   2026-09-24 に vi(bss_end = 16288)で実際に踏んだ。
 *
 *   値は loader.c の IMG_BUDGET と一致させること(言語が違うので
 *   共有できない。片方を変えたら必ずもう片方も)。 */
/* ADDR(.text) を引いて **VMA 相対**で見る。通常ビルドは VMA=0 なので同じだが、
 * 位置独立の検証(同じ .o を別 VMA でリンクしてバイト比較する手口)で
 * 高位番地へリンクしたときに誤って弾かないため。予算は「像の大きさ」の
 * 話であって「番地」の話ではない。 */
ASSERT(__bss_end - ADDR(.text) <= 0x6000,
       "image+BSS exceeds IMG_BUDGET (0x6000). See loader.c IMG_BUDGET.")
