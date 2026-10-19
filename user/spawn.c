/* user/spawn.c - #35 プロセス分割の実証(親側)
 *
 *   「大きなプログラムを本体 + コマンドに割り、子プロセスが親のバッファを
 *   更新する」という構成が tizix で成立するかを確かめる最小例。
 *   確かめるのは 4 点:
 *     (1) 普通のコマンド(sh 以外)から子を起動できるか       → krun_wait
 *     (2) 子の終了を待って続きを実行できるか                → krun_wait の戻り
 *     (3) 子が親のバッファを直接書けるか(保護が無いので)
 *     (4) そのために **tzcc のポインタを絶対番地へ直す**必要があること
 *
 *   (4) が肝。tzcc のポインタは IY=自ブロック先頭 を基準に参照されるので、
 *   値としては自プロセス内でしか意味を持たない(素で渡すと子は自分の
 *   ブロックの同じオフセットを壊す)。absaddr() で絶対番地へ直してから渡す。
 *
 *   使い方: spawn        … 既定の語で往復
 *           spawn WORD   … 子に書かせる語を指定
 *
 *   掟(tzcc): 関数間で同名ローカル禁止 / 乗除算を書かない / printf 不使用 /
 *             比較は符号なし / sizeof(配列) は 2。
 */
#include "stdio.h"

#define BUFSZ  40
#define PACKSZ 48

static char shared[BUFSZ];      /* 子に書かせる共有バッファ */
static char pack[PACKSZ];       /* 子へ渡す NUL 区切り argv */

/* v を 10 進で pack[di] 以降へ。戻り: 次の書き込み位置。
 * 除算を書かない掟なので、10000/1000/… の減算で桁を作る。 */
static unsigned put_dec(unsigned pd_v, unsigned pd_di)
{
    unsigned pd_scale;
    unsigned pd_dig;
    unsigned pd_seen;

    pd_scale = 10000u;
    pd_seen  = 0;
    for (;;) {
        pd_dig = 0;
        while (pd_v >= pd_scale) {
            pd_v = pd_v - pd_scale;
            pd_dig++;
        }
        if (pd_dig != 0) pd_seen = 1;
        if (pd_seen || pd_scale == 1u) {
            pack[pd_di] = (char)('0' + pd_dig);
            pd_di++;
        }
        if (pd_scale == 1u) break;
        if (pd_scale == 10000u)     pd_scale = 1000u;
        else if (pd_scale == 1000u) pd_scale = 100u;
        else if (pd_scale == 100u)  pd_scale = 10u;
        else                        pd_scale = 1u;
    }
    return pd_di;
}

int main(int argc, char **argv)
{
    unsigned mn_abs;
    unsigned mn_di;
    unsigned mn_k;
    unsigned char mn_rc;
    char *mn_word;

    /* 親が自分の印を置く。子がここを上書きできれば共有が成立している。 */
    shared[0] = 'p'; shared[1] = 'a'; shared[2] = 'r'; shared[3] = 'e';
    shared[4] = 'n'; shared[5] = 't'; shared[6] = 0;

    prs("spawn: buf before = ");
    puts(shared);

    /* ★tzcc の既知バグ(#35 で発見)を避ける形で書いてある。
     *   配列のアドレスは **関数引数の位置で評価すれば絶対番地**になるが、
     *   いったん変数へ代入すると +IY されず素のラベル値が入る
     *   (実測 direct=51421 に対し viavar=112)。iy_reg の
     *   「ポインタ = リテラル代入の穴」と同型。
     *   → **`unsigned a = (unsigned)arr;` と書かない。** 引数で直接渡す。
     *   下の行はその差をそのまま表示して、退行したら test_spawn が気付く。 */
    mn_abs = (unsigned)shared;          /* ← わざと踏んだ値(診断表示用) */
    prs("spawn: base=");
    prnum(getbase());
    prs(" direct=");
    prnum((unsigned)shared);
    prs(" viavar=");
    prnum(mn_abs);
    putchar('\n');

    /* argv[0] = バッファの絶対番地(10 進)、argv[1] = 書かせる語。
     * put_dec には **引数の位置で** 渡すこと(上記のバグ回避)。 */
    mn_di = put_dec((unsigned)shared, 0);
    pack[mn_di] = 0;
    mn_di++;

    mn_word = (argc >= 1 && argv[0] && argv[0][0]) ? argv[0] : "CHILD-WAS-HERE";
    for (mn_k = 0; mn_word[mn_k]; mn_k++) {
        if (mn_di + 2u >= PACKSZ) break;
        pack[mn_di] = mn_word[mn_k];
        mn_di++;
    }
    pack[mn_di] = 0;

    mn_rc = krun_wait("/bin/spawnc.bin", pack, 2);
    if (mn_rc == 0)    { puts("spawn: no free block"); return 1; }
    if (mn_rc == 0xFF) { puts("spawn: /bin/spawnc.bin not found"); return 1; }

    prs("spawn: buf after  = ");
    puts(shared);
    return 0;
}
