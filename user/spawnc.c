/* user/spawnc.c - #35 プロセス分割の実証(子側)
 *
 *   argv[0] = 親のバッファの **絶対番地**(10 進文字列)
 *   argv[1] = そこへ書き込む語
 *
 *   受け取るのは絶対番地なので、tzcc のポインタとして参照してはいけない
 *   (IY=自ブロック先頭 が足されて自分の block を壊す)。poke() で書く。
 *   ここが通れば「本体 + コマンド」へのプロセス分割が成立する。
 *
 *   掟(tzcc): 関数間で同名ローカル禁止 / 乗除算を書かない(10x は 8x+2x)/
 *             printf 不使用 / 比較は符号なし。
 */
#include "stdio.h"

/* 10 進文字列 → unsigned。数字以外が来たら 0。 */
static unsigned to_uint(char *ts)
{
    unsigned tv = 0;
    unsigned tk = 0;
    unsigned t2;

    while (ts[tk]) {
        if (ts[tk] < '0') return 0;
        if (ts[tk] > '9') return 0;
        t2 = tv + tv;        /* 2x */
        tv = t2 + t2;        /* 4x */
        tv = tv + tv;        /* 8x */
        tv = tv + t2;        /* 10x */
        tv = tv + (unsigned)(ts[tk] - '0');
        tk++;
    }
    return tv;
}

int main(int argc, char **argv)
{
    char *mc_word;
    unsigned mc_addr;
    unsigned mc_k;

    if (argc < 2 || argv[0] == 0 || argv[1] == 0) {
        puts("spawnc: usage: spawnc <abs-addr> <word>");
        return 1;
    }

    mc_addr = to_uint(argv[0]);
    if (mc_addr == 0) {
        puts("spawnc: bad addr");
        return 1;
    }

    /* ここが本題: 受け取った **絶対番地** へ直接書く(親のブロック内)。
     * mc_p[k] = ... と書くと IY が足されて自分の block を壊すので poke を使う。 */
    mc_word = argv[1];
    for (mc_k = 0; mc_word[mc_k]; mc_k++)
        poke(mc_addr + mc_k, (unsigned char)mc_word[mc_k]);
    poke(mc_addr + mc_k, 0);

    prs("spawnc: base=");
    prnum(getbase());
    prs(" wrote ");
    prnum(mc_k);
    prs("B at ");
    prnum(mc_addr);
    putchar('\n');
    return 0;
}
