/* user/xblk.c - #38 追加ブロック(像に含めない作業領域)の実証
 *
 *   大きな作業領域を **像に持たない**ための仕組み。.BIN 先頭 32B の予約ヘッダ
 *   (crt0 の `.ds 0x20` = 従来まったくの死に領域)に「像とは別に欲しい
 *   ブロック数」を刻んでおくと、kexec がそのぶん多く確保する。
 *
 *   像に大きな配列を持つと:
 *     ・.BIN が実データの無いゼロで太る(vi の text[1024] がまさにこれ)
 *     ・ロードがそのぶん遅い
 *     ・4KB 単位の切り上げで端数が丸ごと無駄になる
 *   追加ブロックなら像は小さいまま、作業領域だけ 4KB 単位で増やせる。
 *
 *   **SP と argv[] は像側の上端に置かれる**(kexec が crt0 へ像のブロック数を
 *   渡している)ので、追加ブロックは丸ごと自由に使える。中身は初期化されない。
 *
 *   ビルド: make tizixcmd CMD=xblk XBLK=1
 *
 *   掟(tzcc): 関数間で同名ローカル禁止 / 乗除算を書かない / 比較は符号なし /
 *             配列アドレスを変数へ代入しない(#35)。
 */
#include "stdio.h"

int main(int argc, char **argv)
{
    unsigned xb;
    unsigned xn;
    unsigned xi;
    unsigned bad;

    (void)argc;
    (void)argv;

    xb = getxbase();
    xn = getxsize();
    /* 追加ブロックの **上端はスタックと argv[] が使う**(SP はプロセス最上端から
     * 下りてくる)。全部を書くとスタックを潰すので余白を残す。 */
    if (xn > 1536) xn = xn - 1536;
    else xn = 0;

    prs("xblk: base=");
    prnum(getbase());
    prs(" xbase=");
    prnum(xb);
    prs(" usable=");
    prnum(xn);
    putchar('\n');

    if (xn == 0) {
        puts("xblk: no extra block (header not stamped?)");
        return 1;
    }

    /* 追加ブロックは自分のブロックより **上**にあること */
    if (xb <= getbase()) {
        puts("xblk: xbase must be above base");
        return 1;
    }

    /* 全域に書いて読み返す。像やスタックを踏んでいれば化ける。
     * 4KB を 1 バイトずつ触るので少し時間がかかる(実機なら数十 ms)。 */
    xi = 0;
    while (xi < xn) {
        poke(xb + xi, (unsigned char)(xi & 0xFF));
        xi++;
    }
    bad = 0;
    xi = 0;
    while (xi < xn) {
        if (peek(xb + xi) != (unsigned char)(xi & 0xFF)) bad++;
        xi++;
    }

    prs("xblk: wrote+verified ");
    prnum(xn);
    prs(" bytes, bad=");
    prnum(bad);
    putchar('\n');

    if (bad != 0) return 1;
    puts("xblk: OK");
    return 0;
}
